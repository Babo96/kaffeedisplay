#include "kaffee/board_esp.hpp"

#include <inttypes.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>

#include "board_pins.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kaffee/touch_transform.hpp"
#include "st7701_type9.hpp"

namespace kaffee {
namespace {

constexpr const char* kTag = "board";

// The flush wrapper is display-wide and the board owns exactly one display.
EspDisplay* g_flush_self = nullptr;

// The LEDC divider must be >= 1. On the ESP32-S3 APB clock (80 MHz),
// 20 kHz * 2^12 = 81.92 MHz exceeds the clock, so 19 kHz is the closest
// 12-bit setting that is actually reachable.
constexpr uint32_t kLedcFreqHz = 19000;
constexpr uint32_t kLedcResolutionBits = 12;
constexpr uint32_t kLedcMaxDuty = (1U << kLedcResolutionBits) - 1;
constexpr uint32_t kMinBacklightDuty = 32;  // keeps 1 % visibly on

static_assert(80000000ULL >= kLedcFreqHz * (1ULL << kLedcResolutionBits),
              "LEDC frequency at 12-bit must fit the 80 MHz source clock");
static_assert(80000000ULL < 20000ULL * (1ULL << kLedcResolutionBits),
              "20 kHz at 12-bit must not be used on the ESP32-S3");

constexpr int kBacklightTimer = LEDC_TIMER_0;
constexpr int kBacklightChannel = LEDC_CHANNEL_0;

uint16_t s_gamma_lut[101] = {};
bool s_gamma_ready = false;

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

lv_display_rotation_t to_lvgl_rotation(int rotation) {
    switch (rotation) {
        case 90:
            return LV_DISPLAY_ROTATION_90;
        case 180:
            return LV_DISPLAY_ROTATION_180;
        case 270:
            return LV_DISPLAY_ROTATION_270;
        default:
            return LV_DISPLAY_ROTATION_0;
    }
}

}  // namespace

Result<std::unique_ptr<EspDisplay>> EspDisplay::create() {
    auto display = std::unique_ptr<EspDisplay>(new EspDisplay());
    display->state_mutex_ = xSemaphoreCreateMutex();
    display->backlight_mutex_ = xSemaphoreCreateMutex();
    display->activity_mutex_ = xSemaphoreCreateMutex();
    if (display->state_mutex_ == nullptr || display->backlight_mutex_ == nullptr ||
        display->activity_mutex_ == nullptr) {
        return Result<std::unique_ptr<EspDisplay>>::fail("board/no_mem",
                                                         "cannot allocate display mutexes");
    }
    return Result<std::unique_ptr<EspDisplay>>::ok(std::move(display));
}

EspDisplay::~EspDisplay() {
    stop_tasks();

    if (lvgl_ready_.load()) {
        if (lock(1000)) {
            destroy_test_pattern_locked();
            if (indev_timer_ != nullptr) {
                lv_timer_delete(indev_timer_);
                indev_timer_ = nullptr;
            }
            if (lv_indev_ != nullptr) {
                lv_indev_delete(lv_indev_);
                lv_indev_ = nullptr;
            }
            unlock();
        }
        // With avoid_tearing the LVGL draw buffers are the panel's two scanout
        // frame buffers. lvgl_port_remove_disp() would free them and double-free
        // the RGB panel's memory. The display is static for the firmware
        // lifetime; stop the port task without removing the display.
        lvgl_port_deinit();
        lv_display_ = nullptr;
        lvgl_ready_.store(false);
    }

    apply_backlight(0, false);

    if (temperature_sensor_ != nullptr) {
        temperature_sensor_disable(temperature_sensor_);
        temperature_sensor_uninstall(temperature_sensor_);
        temperature_sensor_ = nullptr;
    }
    if (touch_ != nullptr) {
        esp_lcd_touch_del(touch_);
        touch_ = nullptr;
    }
    if (touch_io_ != nullptr) {
        esp_lcd_panel_io_del(touch_io_);
        touch_io_ = nullptr;
    }
    if (i2c_bus_ != nullptr) {
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
    }
    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
        panel_ = nullptr;
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
        panel_io_ = nullptr;
    }
    if (state_mutex_ != nullptr) {
        vSemaphoreDelete(state_mutex_);
        state_mutex_ = nullptr;
    }
    if (backlight_mutex_ != nullptr) {
        vSemaphoreDelete(backlight_mutex_);
        backlight_mutex_ = nullptr;
    }
    if (activity_mutex_ != nullptr) {
        vSemaphoreDelete(activity_mutex_);
        activity_mutex_ = nullptr;
    }
}

Result<Unit> EspDisplay::init() {
    if (panel_ != nullptr) {
        return Result<Unit>::ok(Unit{});
    }
    if (state_mutex_ == nullptr || backlight_mutex_ == nullptr || activity_mutex_ == nullptr) {
        return Result<Unit>::fail("board/not_created", "display mutexes are missing");
    }

    Result<Unit> result = init_backlight();
    if (!result) {
        return result;
    }
    result = init_panel();
    if (!result) {
        return result;
    }

    // Touch and the temperature sensor are best-effort: a missing GT911 must
    // not prevent the kiosk from showing its booking page.
    if (!init_touch()) {
        ESP_LOGW(kTag, "GT911 unavailable at boot; will retry in the touch task");
    }
    init_temperature_sensor();

    last_activity_ms_ = now_ms();
    power_state_.store(static_cast<int>(PowerState::Off));
    set_power_state(PowerState::Active, true);
    start_tasks();

    ESP_LOGI(kTag, "board ready: %dx%d, rotation %d, active %d%%, idle %d%%", kWidth, kHeight,
             settings_.rotation, settings_.brightness_active, settings_.brightness_idle);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> EspDisplay::init_backlight() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_12_BIT;
    timer.timer_num = static_cast<ledc_timer_t>(kBacklightTimer);
    timer.freq_hz = kLedcFreqHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "LEDC timer init failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("board/backlight", "LEDC timer configuration failed");
    }

    // Boot-time verification: a divider below 1 (the 20 kHz/12-bit case) is
    // rejected by the driver or silently misconfigured, so read it back.
    const uint32_t configured_hz =
        ledc_get_freq(LEDC_LOW_SPEED_MODE, static_cast<ledc_timer_t>(kBacklightTimer));
    const uint32_t tolerance = kLedcFreqHz / 20;  // 5 %
    if (configured_hz == 0 || configured_hz < kLedcFreqHz - tolerance ||
        configured_hz > kLedcFreqHz + tolerance) {
        ESP_LOGE(kTag, "LEDC frequency check failed: requested %" PRIu32 " Hz, got %" PRIu32 " Hz",
                 kLedcFreqHz, configured_hz);
        return Result<Unit>::fail("board/backlight",
                                  "LEDC timer cannot produce the requested frequency");
    }
    ESP_LOGI(kTag, "backlight PWM: %" PRIu32 " Hz, %" PRIu32 "-bit, max duty %" PRIu32,
             configured_hz, kLedcResolutionBits, kLedcMaxDuty);

    ledc_channel_config_t channel = {};
    channel.gpio_num = board::kPinBacklight;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = static_cast<ledc_channel_t>(kBacklightChannel);
    channel.intr_type = LEDC_INTR_DISABLE;
    channel.timer_sel = static_cast<ledc_timer_t>(kBacklightTimer);
    channel.duty = 0;
    channel.hpoint = 0;
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "LEDC channel init failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("board/backlight", "LEDC channel configuration failed");
    }

    // Gamma 2.2 makes low backlight percentages visually linear. A lookup
    // table keeps the 100 Hz touch/wake path free of floating point work.
    for (int i = 0; i <= 100; ++i) {
        const float normalized = static_cast<float>(i) / 100.0f;
        s_gamma_lut[i] = static_cast<uint16_t>(
            std::lroundf(std::pow(normalized, 2.2f) * static_cast<float>(kLedcMaxDuty)));
    }
    s_gamma_ready = true;

    // Fades are a nicety; the display still works with instant transitions.
    fade_available_ = ledc_fade_func_install(0) == ESP_OK;
    if (!fade_available_) {
        ESP_LOGW(kTag, "LEDC fade unavailable; using instant backlight changes");
    }

    return Result<Unit>::ok(Unit{});
}

Result<Unit> EspDisplay::init_panel() {
    spi_line_config_t line = {};
    line.cs_io_type = IO_TYPE_GPIO;
    line.cs_gpio_num = board::kPinLcdSpiCs;
    line.scl_io_type = IO_TYPE_GPIO;
    line.scl_gpio_num = board::kPinLcdSpiSck;
    line.sda_io_type = IO_TYPE_GPIO;
    line.sda_gpio_num = board::kPinLcdSpiMosi;
    line.io_expander = nullptr;

    esp_lcd_panel_io_3wire_spi_config_t io_config = {};
    io_config.line_config = line;
    io_config.expect_clk_speed = PANEL_IO_3WIRE_SPI_CLK_MAX;
    io_config.spi_mode = 0;
    io_config.lcd_cmd_bytes = 1;
    io_config.lcd_param_bytes = 1;
    io_config.flags.use_dc_bit = 1;
    io_config.flags.dc_zero_on_data = 0;
    io_config.flags.lsb_first = 0;
    io_config.flags.cs_high_active = 0;
    io_config.flags.del_keep_cs_inactive = 1;

    esp_err_t err = esp_lcd_new_panel_io_3wire_spi(&io_config, &panel_io_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ST7701 3-wire SPI init failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("board/panel_io", "3-wire SPI panel IO creation failed");
    }

    esp_lcd_rgb_panel_config_t rgb_config = {};
    rgb_config.clk_src = LCD_CLK_SRC_DEFAULT;
    rgb_config.data_width = 16;
    rgb_config.bits_per_pixel = 16;
    // Two scanout framebuffers: esp_lvgl_port swaps them at VSYNC when
    // avoid_tearing is enabled, so drawing never overlaps the visible frame.
    rgb_config.num_fbs = 2;
    rgb_config.de_gpio_num = board::kPinLcdDe;
    rgb_config.pclk_gpio_num = board::kPinLcdPclk;
    rgb_config.vsync_gpio_num = board::kPinLcdVsync;
    rgb_config.hsync_gpio_num = board::kPinLcdHsync;
    rgb_config.disp_gpio_num = GPIO_NUM_NC;
    rgb_config.dma_burst_size = 64;
    for (int i = 0; i < 16; ++i) {
        rgb_config.data_gpio_nums[i] = board::kLcdDataGpio[i];
    }
    rgb_config.timings.pclk_hz = board::kLcdPclkHz;
    rgb_config.timings.h_res = kWidth;
    rgb_config.timings.v_res = kHeight;
    rgb_config.timings.hsync_pulse_width = board::kLcdHsyncPulse;
    rgb_config.timings.hsync_back_porch = board::kLcdHsyncBackPorch;
    rgb_config.timings.hsync_front_porch = board::kLcdHsyncFrontPorch;
    rgb_config.timings.vsync_pulse_width = board::kLcdVsyncPulse;
    rgb_config.timings.vsync_back_porch = board::kLcdVsyncBackPorch;
    rgb_config.timings.vsync_front_porch = board::kLcdVsyncFrontPorch;
    rgb_config.timings.flags.pclk_active_neg = false;
    // The 480x480 RGB scanout reads the PSRAM framebuffer at 12 MHz. Without
    // bounce buffers a PSRAM hiccup underruns the FIFO and the panel latches
    // the line late, shifting/wrapping the image horizontally. Six lines of
    // internal-RAM bounce buffer absorb that without visible tearing while
    // leaving enough SRAM for the serial console task.
    rgb_config.bounce_buffer_size_px = 5 * kWidth;
    rgb_config.flags.fb_in_psram = 1;

    static st7701_lcd_init_cmd_t init_cmds[board::st7701::kType9InitCount];
    const std::size_t init_count =
        board::st7701::build_init_commands(init_cmds, board::st7701::kType9InitCount);
    if (init_count == 0) {
        return Result<Unit>::fail("board/init_sequence", "ST7701 init sequence is empty");
    }

    st7701_vendor_config_t vendor = {};
    vendor.rgb_config = &rgb_config;
    vendor.init_cmds = init_cmds;
    vendor.init_cmds_size = static_cast<uint16_t>(init_count);
    vendor.flags.mirror_by_cmd = 0;
    vendor.flags.enable_io_multiplex = 0;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor;

    err = esp_lcd_new_panel_st7701(panel_io_, &panel_config, &panel_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ST7701 panel creation failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("board/panel", "ST7701 panel creation failed");
    }

    err = esp_lcd_panel_reset(panel_);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "ST7701 reset skipped: %s", esp_err_to_name(err));
    }
    err = esp_lcd_panel_init(panel_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ST7701 init failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("board/panel_init", "ST7701 init sequence failed");
    }
    err = esp_lcd_panel_disp_on_off(panel_, true);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "ST7701 display-on skipped: %s", esp_err_to_name(err));
    }

    // Clear the PSRAM frame buffer before the backlight comes on, otherwise
    // power-on RAM content can flash on the panel.
    void* fb0 = nullptr;
    if (esp_lcd_rgb_panel_get_frame_buffer(panel_, 1, &fb0) == ESP_OK) {
        const size_t frame_bytes = static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t);
        std::memset(fb0, 0, frame_bytes);
    } else {
        ESP_LOGW(kTag, "could not access RGB frame buffer for clearing");
    }

    ESP_LOGI(kTag, "ST7701S RGB panel initialized (%dx%d, type-9, single frame buffer)", kWidth,
             kHeight);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> EspDisplay::run_scanout_selftest() {
    if (panel_ == nullptr) {
        return Result<Unit>::fail("board/not_initialized", "panel must be initialized first");
    }
    void* framebuffer = nullptr;
    const esp_err_t fb_err = esp_lcd_rgb_panel_get_frame_buffer(panel_, 1, &framebuffer);
    if (fb_err != ESP_OK || framebuffer == nullptr) {
        ESP_LOGE(kTag, "scanout self-test: no framebuffer: %s", esp_err_to_name(fb_err));
        return Result<Unit>::fail("board/no_framebuffer", "RGB framebuffer is unavailable");
    }

    auto* pixels = static_cast<uint16_t*>(framebuffer);
    const size_t pixel_count = static_cast<size_t>(kWidth) * kHeight;
    const size_t byte_count = pixel_count * sizeof(uint16_t);

    auto publish = [&](const char* label) {
        const esp_err_t sync =
            esp_cache_msync(framebuffer, byte_count, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        ESP_LOGI(kTag, "scanout self-test: %s, cache_msync=%s", label, esp_err_to_name(sync));
    };

    auto solid = [&](uint16_t color, const char* name) {
        for (size_t i = 0; i < pixel_count; ++i) {
            pixels[i] = color;
        }
        publish(name);
        vTaskDelay(pdMS_TO_TICKS(2000));
    };

    ESP_LOGI(kTag, "scanout self-test: framebuffer %p, %zu bytes", framebuffer, byte_count);
    solid(0xF800, "red (0xF800)");
    solid(0x07E0, "green (0x07E0)");
    solid(0x001F, "blue (0x001F)");
    solid(0xFFFF, "white (0xFFFF)");

    // Eight classic vertical colour bars, left to right: white, yellow, cyan,
    // green, magenta, red, blue, black.
    static constexpr uint16_t kBars[8] = {0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
                                          0xF81F, 0xF800, 0x001F, 0x0000};
    const int bar_width = kWidth / 8;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            pixels[static_cast<size_t>(y) * kWidth + x] = kBars[x / bar_width];
        }
    }
    publish("8 vertical bars (white/yellow/cyan/green/magenta/red/blue/black)");
    ESP_LOGI(kTag, "scanout self-test complete; bars are left on the panel for inspection");
    return Result<Unit>::ok(Unit{});
}

Result<Unit> EspDisplay::init_touch() {
    if (i2c_bus_ == nullptr) {
        i2c_master_bus_config_t bus = {};
        bus.i2c_port = static_cast<i2c_port_num_t>(board::kTouchI2cPort);
        bus.sda_io_num = static_cast<gpio_num_t>(board::kPinTouchSda);
        bus.scl_io_num = static_cast<gpio_num_t>(board::kPinTouchScl);
        bus.clk_source = I2C_CLK_SRC_DEFAULT;
        bus.glitch_ignore_cnt = 7;
        bus.flags.enable_internal_pullup = 1;
        esp_err_t err = i2c_new_master_bus(&bus, &i2c_bus_);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "touch I2C bus init failed: %s", esp_err_to_name(err));
            i2c_bus_ = nullptr;
            return Result<Unit>::fail("board/touch_i2c", "I2C bus creation failed");
        }
    }

    uint8_t address = board::kTouchAddress;
    esp_err_t probe = i2c_master_probe(i2c_bus_, address, 100);
    if (probe != ESP_OK) {
        address = board::kTouchAddressBackup;
        probe = i2c_master_probe(i2c_bus_, address, 100);
    }
    if (probe != ESP_OK) {
        ESP_LOGW(kTag, "GT911 not found at 0x5D or 0x14");
        return Result<Unit>::fail("board/touch", "GT911 did not acknowledge on I2C");
    }

    if (touch_ != nullptr) {
        esp_lcd_touch_del(touch_);
        touch_ = nullptr;
    }
    if (touch_io_ != nullptr) {
        esp_lcd_panel_io_del(touch_io_);
        touch_io_ = nullptr;
    }

    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = address;
    io_config.scl_speed_hz = 400000;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 16;
    io_config.flags.disable_control_phase = 1;
    io_config.transaction_timeout_ms = 100;

    esp_err_t err = esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &touch_io_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "GT911 panel IO init failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("board/touch_io", "GT911 I2C panel IO creation failed");
    }

    gt911_driver_cfg_.dev_addr = address;

    esp_lcd_touch_config_t touch_config = {};
    touch_config.x_max = kWidth - 1;
    touch_config.y_max = kHeight - 1;
    touch_config.rst_gpio_num = GPIO_NUM_NC;
    touch_config.int_gpio_num = GPIO_NUM_NC;
    touch_config.levels.reset = 0;
    touch_config.levels.interrupt = 0;
    touch_config.flags.swap_xy = 0;
    touch_config.flags.mirror_x = 0;
    touch_config.flags.mirror_y = 0;
    touch_config.process_coordinates = nullptr;
    touch_config.driver_data = &gt911_driver_cfg_;

    for (int attempt = 0; attempt < 3; ++attempt) {
        err = esp_lcd_touch_new_i2c_gt911(touch_io_, &touch_config, &touch_);
        if (err == ESP_OK) {
            touch_present_.store(true);
            ESP_LOGI(kTag, "GT911 ready at 0x%02X (polled at 100 Hz)", address);
            return Result<Unit>::ok(Unit{});
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGE(kTag, "GT911 init failed: %s", esp_err_to_name(err));
    esp_lcd_panel_io_del(touch_io_);
    touch_io_ = nullptr;
    touch_present_.store(false);
    return Result<Unit>::fail("board/touch_init", "GT911 driver initialization failed");
}

Result<Unit> EspDisplay::recover_touch() {
    touch_present_.store(false);
    if (touch_ != nullptr) {
        esp_lcd_touch_del(touch_);
        touch_ = nullptr;
    }
    if (touch_io_ != nullptr) {
        esp_lcd_panel_io_del(touch_io_);
        touch_io_ = nullptr;
    }
    return init_touch();
}

Result<Unit> EspDisplay::init_temperature_sensor() {
    temperature_sensor_config_t config = {};
    config.range_min = -10;
    config.range_max = 80;
    config.clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT;
    esp_err_t err = temperature_sensor_install(&config, &temperature_sensor_);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "chip temperature sensor install failed: %s", esp_err_to_name(err));
        temperature_sensor_ = nullptr;
        return Result<Unit>::fail("board/temp", "temperature sensor install failed");
    }
    err = temperature_sensor_enable(temperature_sensor_);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "chip temperature sensor enable failed: %s", esp_err_to_name(err));
        temperature_sensor_uninstall(temperature_sensor_);
        temperature_sensor_ = nullptr;
        return Result<Unit>::fail("board/temp", "temperature sensor enable failed");
    }
    return Result<Unit>::ok(Unit{});
}

void EspDisplay::start_tasks() {
    if (running_.exchange(true)) {
        return;
    }
    task_count_.store(0);
    BaseType_t ok = xTaskCreatePinnedToCore(&EspDisplay::touch_task_entry, "board_touch", 4096,
                                            this, 4, &touch_task_, tskNO_AFFINITY);
    if (ok == pdPASS) {
        task_count_.fetch_add(1);
    } else {
        ESP_LOGE(kTag, "failed to create touch task");
        touch_task_ = nullptr;
    }
    ok = xTaskCreatePinnedToCore(&EspDisplay::power_task_entry, "board_power", 3072, this, 3,
                                 &power_task_, tskNO_AFFINITY);
    if (ok == pdPASS) {
        task_count_.fetch_add(1);
    } else {
        ESP_LOGE(kTag, "failed to create power task");
        power_task_ = nullptr;
    }
    if (task_count_.load() == 0) {
        running_.store(false);
    }
}

void EspDisplay::stop_tasks() {
    if (!running_.exchange(false)) {
        return;
    }
    // Both loops notice running_ and remove themselves. Wait for the counter
    // instead of assuming a fixed worst-case delay.
    for (int waited_ms = 0; waited_ms < 1000 && task_count_.load() > 0; waited_ms += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (task_count_.load() > 0) {
        ESP_LOGW(kTag, "tasks did not stop in time; forcing deletion");
        if (touch_task_ != nullptr) {
            vTaskDelete(touch_task_);
        }
        if (power_task_ != nullptr) {
            vTaskDelete(power_task_);
        }
    }
    touch_task_ = nullptr;
    power_task_ = nullptr;
}

void EspDisplay::touch_task_entry(void* arg) {
    static_cast<EspDisplay*>(arg)->touch_loop();
}

void EspDisplay::power_task_entry(void* arg) {
    static_cast<EspDisplay*>(arg)->power_loop();
}

void EspDisplay::touch_loop() {
    TickType_t last_wake = xTaskGetTickCount();
    int consecutive_failures = 0;
    int no_touch_ticks = 0;

    while (running_.load()) {
        bool pressed = false;
        int32_t raw_x = 0;
        int32_t raw_y = 0;

        if (touch_ == nullptr) {
            // A GT911 that was absent at boot is retried every five seconds.
            if (++no_touch_ticks >= 500) {
                no_touch_ticks = 0;
                init_touch();
            }
        } else {
            no_touch_ticks = 0;
            const esp_err_t err = esp_lcd_touch_read_data(touch_);
            if (err == ESP_OK) {
                esp_lcd_touch_point_data_t points[1] = {};
                uint8_t count = 0;
                if (esp_lcd_touch_get_data(touch_, points, &count, 1) == ESP_OK && count > 0) {
                    raw_x = points[0].x;
                    raw_y = points[0].y;
                    pressed = true;
                }
                consecutive_failures = 0;
            } else {
                ++consecutive_failures;
            }
        }

        if (pressed) {
            // LVGL rotates pointer input itself (lv_display_rotate_point), so
            // publish raw panel coordinates here. The test pattern applies the
            // same transform explicitly for its marker.
            publish_touch(raw_x, raw_y, true);
            note_activity();
        } else {
            const TouchSample previous = read_touch();
            publish_touch(previous.x, previous.y, false);
            if (consecutive_failures >= 200) {
                ESP_LOGW(kTag, "GT911 stopped responding; reinitialising");
                consecutive_failures = 0;
                recover_touch();
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kTouchPollMs));
    }
    task_count_.fetch_sub(1);
    vTaskDelete(nullptr);
}

void EspDisplay::power_loop() {
    while (running_.load()) {
        // Sleep in short slices so shutdown is bounded even though the state
        // machine only needs a 200 ms tick.
        for (int slept = 0; slept < kPowerTickMs && running_.load(); slept += 20) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!running_.load()) {
            break;
        }

        if (activity_mutex_ != nullptr) {
            xSemaphoreTake(activity_mutex_, portMAX_DELAY);
        }
        const DisplaySettings current = snapshot_settings();
        const uint32_t elapsed_ms = now_ms() - last_activity_ms_;
        PowerState desired = PowerState::Active;

        if (stay_awake_) {
            desired = PowerState::Active;
        } else if (current.screen_off_timeout_s > 0 &&
                   elapsed_ms >= static_cast<uint32_t>(current.screen_off_timeout_s) * 1000U) {
            desired = PowerState::Off;
        } else if (current.dim_timeout_s > 0 &&
                   elapsed_ms >= static_cast<uint32_t>(current.dim_timeout_s) * 1000U) {
            desired = PowerState::Idle;
        }

        if (static_cast<PowerState>(power_state_.load()) != desired) {
            set_power_state(desired, true);
        }
        if (activity_mutex_ != nullptr) {
            xSemaphoreGive(activity_mutex_);
        }
    }
    task_count_.fetch_sub(1);
    vTaskDelete(nullptr);
}

void EspDisplay::touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<EspDisplay*>(lv_indev_get_user_data(indev));
    if (self == nullptr || data == nullptr) {
        return;
    }
    if (self->synthetic_swipe_active_.load()) {
        const int step = self->synthetic_swipe_step_.fetch_add(1);
        const int steps = self->synthetic_swipe_steps_.load();
        if (step <= steps) {
            const int x0 = self->synthetic_swipe_x0_.load();
            const int y0 = self->synthetic_swipe_y0_.load();
            const int x1 = self->synthetic_swipe_x1_.load();
            const int y1 = self->synthetic_swipe_y1_.load();
            data->point.x = static_cast<int32_t>(x0 + (x1 - x0) * step / steps);
            data->point.y = static_cast<int32_t>(y0 + (y1 - y0) * step / steps);
            data->state = LV_INDEV_STATE_PRESSED;
            data->continue_reading = false;
            return;
        }
        self->synthetic_swipe_active_.store(false);
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = false;
        return;
    }
    if (self->synthetic_touch_pending_.exchange(false)) {
        data->point.x = self->synthetic_touch_x_.load();
        data->point.y = self->synthetic_touch_y_.load();
        data->state = LV_INDEV_STATE_PRESSED;
        data->continue_reading = false;
        return;
    }
    const TouchSample sample = self->read_touch();
    data->point.x = sample.x;
    data->point.y = sample.y;
    data->state = sample.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
}

bool EspDisplay::inject_touch(int x, int y) {
    if (!lvgl_ready_.load() || lv_indev_ == nullptr) return false;
    synthetic_swipe_active_.store(false);
    synthetic_touch_x_.store(static_cast<uint16_t>(clamp_int(x, 0, kWidth - 1)));
    synthetic_touch_y_.store(static_cast<uint16_t>(clamp_int(y, 0, kHeight - 1)));
    synthetic_touch_pending_.store(true);
    return true;
}

bool EspDisplay::inject_swipe(int x1, int y1, int x2, int y2) {
    if (!lvgl_ready_.load() || lv_indev_ == nullptr) return false;
    synthetic_touch_pending_.store(false);
    synthetic_swipe_x0_.store(clamp_int(x1, 0, kWidth - 1));
    synthetic_swipe_y0_.store(clamp_int(y1, 0, kHeight - 1));
    synthetic_swipe_x1_.store(clamp_int(x2, 0, kWidth - 1));
    synthetic_swipe_y1_.store(clamp_int(y2, 0, kHeight - 1));
    synthetic_swipe_steps_.store(12);
    synthetic_swipe_step_.store(0);
    synthetic_swipe_active_.store(true);
    return true;
}

void EspDisplay::publish_touch(int32_t x, int32_t y, bool pressed) {
    portENTER_CRITICAL(&touch_lock_);
    touch_sample_.x = static_cast<uint16_t>(clamp_int(x, 0, kWidth - 1));
    touch_sample_.y = static_cast<uint16_t>(clamp_int(y, 0, kHeight - 1));
    touch_sample_.pressed = pressed;
    portEXIT_CRITICAL(&touch_lock_);
}

EspDisplay::TouchSample EspDisplay::read_touch() const {
    portENTER_CRITICAL(&touch_lock_);
    const TouchSample sample = touch_sample_;
    portEXIT_CRITICAL(&touch_lock_);
    return sample;
}

DisplaySettings EspDisplay::snapshot_settings() const {
    DisplaySettings copy;
    if (state_mutex_ != nullptr) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        copy = settings_;
        xSemaphoreGive(state_mutex_);
    } else {
        copy = settings_;
    }
    return copy;
}

void EspDisplay::note_activity() {
    if (activity_mutex_ != nullptr) {
        xSemaphoreTake(activity_mutex_, portMAX_DELAY);
    }
    last_activity_ms_ = now_ms();
    if (static_cast<PowerState>(power_state_.load()) != PowerState::Active) {
        set_power_state(PowerState::Active, true);
    }
    if (activity_mutex_ != nullptr) {
        xSemaphoreGive(activity_mutex_);
    }
}

void EspDisplay::set_stay_awake(bool stay_awake) {
    if (activity_mutex_ != nullptr) {
        xSemaphoreTake(activity_mutex_, portMAX_DELAY);
    }
    stay_awake_ = stay_awake;
    // Enable/disable restarts the idle window instead of immediately dimming.
    last_activity_ms_ = now_ms();
    if (stay_awake) {
        set_power_state(PowerState::Active, true);
    }
    if (activity_mutex_ != nullptr) {
        xSemaphoreGive(activity_mutex_);
    }
    ESP_LOGI(kTag, "stay-awake %s", stay_awake ? "enabled" : "disabled");
}

void EspDisplay::set_power_state(PowerState state, bool fade) {
    const DisplaySettings current = snapshot_settings();
    int target = 0;
    switch (state) {
        case PowerState::Active:
            target = current.brightness_active;
            break;
        case PowerState::Idle:
            target = current.brightness_idle;
            break;
        case PowerState::Off:
        default:
            target = 0;
            break;
    }

    if (static_cast<PowerState>(power_state_.load()) == state &&
        current_percent_.load() == target) {
        return;
    }
    power_state_.store(static_cast<int>(state));
    apply_backlight(target, fade);
}

void EspDisplay::apply_backlight(int percent, bool fade) {
    percent = clamp_int(percent, 0, 100);
    if (backlight_mutex_ == nullptr) {
        return;
    }

    xSemaphoreTake(backlight_mutex_, portMAX_DELAY);

    if (percent == backlight_target_percent_) {
        current_percent_.store(percent);
        xSemaphoreGive(backlight_mutex_);
        return;
    }

    const uint32_t duty = percent_to_duty(percent);
    if (fade && fade_available_) {
        // A previous fade may still be running; stop it before retargeting.
        ledc_fade_stop(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(kBacklightChannel));
        const esp_err_t err = ledc_set_fade_with_time(
            LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(kBacklightChannel), duty,
            kBacklightFadeMs);
        if (err == ESP_OK) {
            ledc_fade_start(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(kBacklightChannel),
                            LEDC_FADE_NO_WAIT);
        } else {
            ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE,
                                     static_cast<ledc_channel_t>(kBacklightChannel), duty, 0);
        }
    } else {
        ledc_fade_stop(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(kBacklightChannel));
        ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE,
                                 static_cast<ledc_channel_t>(kBacklightChannel), duty, 0);
    }

    backlight_target_percent_ = percent;
    current_percent_.store(percent);
    xSemaphoreGive(backlight_mutex_);
}

uint32_t EspDisplay::percent_to_duty(int percent) const {
    percent = clamp_int(percent, 0, 100);
    if (percent == 0) {
        return 0;
    }
    uint32_t duty = 0;
    if (!s_gamma_ready) {
        duty = static_cast<uint32_t>(percent) * kLedcMaxDuty / 100U;
    } else {
        duty = s_gamma_lut[percent];
    }
    // Gamma 2.2 maps 1 % to <1 duty count. Keep any non-zero setting visibly
    // on instead of turning the panel black at the lowest brightness step.
    return duty < kMinBacklightDuty ? kMinBacklightDuty : duty;
}

void EspDisplay::set_backlight_percent(int percent, bool fade) {
    percent = clamp_int(percent, 0, 100);
    if (activity_mutex_ != nullptr) {
        xSemaphoreTake(activity_mutex_, portMAX_DELAY);
    }
    apply_backlight(percent, fade);
    power_state_.store(static_cast<int>(percent > 0 ? PowerState::Active : PowerState::Off));
    if (activity_mutex_ != nullptr) {
        xSemaphoreGive(activity_mutex_);
    }
}

void EspDisplay::wake() {
    note_activity();
}

DisplayInfo EspDisplay::info() const {
    DisplayInfo result;
    result.width = kWidth;
    result.height = kHeight;
    result.rotation = rotation_.load();
    result.brightness_percent = current_percent_.load();
    result.backlight_on = result.brightness_percent > 0;
    result.ui_fps = static_cast<float>(ui_fps_milli_.load()) / 1000.0f;
    result.flush_ms = static_cast<float>(flush_last_us_.load()) / 1000.0f;
    result.render_ms = static_cast<float>(render_last_us_.load()) / 1000.0f;
    // 1 / (h_total * v_total / pclk) for the configured RGB timings.
    const int h_total =
        kWidth + board::kLcdHsyncPulse + board::kLcdHsyncBackPorch + board::kLcdHsyncFrontPorch;
    const int v_total =
        kHeight + board::kLcdVsyncPulse + board::kLcdVsyncBackPorch + board::kLcdVsyncFrontPorch;
    const double frame_s = static_cast<double>(h_total) * v_total / board::kLcdPclkHz;
    result.panel_hz = frame_s > 0 ? static_cast<float>(1.0 / frame_s) : 0.0f;
    return result;
}

RgbTiming EspDisplay::rgb_timing() const {
    RgbTiming timing;
    timing.pclk_hz = board::kLcdPclkHz;
    timing.hsync_pulse_width = board::kLcdHsyncPulse;
    timing.hsync_back_porch = board::kLcdHsyncBackPorch;
    timing.hsync_front_porch = board::kLcdHsyncFrontPorch;
    timing.vsync_pulse_width = board::kLcdVsyncPulse;
    timing.vsync_back_porch = board::kLcdVsyncBackPorch;
    timing.vsync_front_porch = board::kLcdVsyncFrontPorch;
    return timing;
}

DisplaySettings EspDisplay::settings() const {
    return snapshot_settings();
}

Result<Unit> EspDisplay::apply_settings(const DisplaySettings& settings) {
    DisplaySettings next = settings;
    // Only 0 is implemented (see init_lvgl); clamp instead of failing the whole
    // apply so a device that stored 90 with older firmware still boots usable.
    if (next.rotation != 0) {
        ESP_LOGW(kTag, "rotation %d is not supported on this panel; using 0", next.rotation);
        next.rotation = 0;
    }
    next.rotation = touch::normalize_rotation(next.rotation);
    next.brightness_active = clamp_int(next.brightness_active, 5, 100);
    next.brightness_idle = clamp_int(next.brightness_idle, 1, 100);
    if (next.dim_timeout_s < 0) {
        next.dim_timeout_s = 0;
    }
    if (next.screen_off_timeout_s < 0) {
        next.screen_off_timeout_s = 0;
    }

    const int previous_rotation = rotation_.load();
    if (next.rotation != previous_rotation) {
        if (lvgl_ready_.load()) {
            // Direct full-frame flushes can hold the LVGL lock across a VSYNC
            // wait; allow a few frames for a rotation change.
            if (!lock(3000)) {
                return Result<Unit>::fail("board/display_busy",
                                          "LVGL did not yield for rotation change");
            }
            apply_rotation_locked(next.rotation);
            unlock();
        }
        rotation_.store(next.rotation);
    }

    {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        settings_ = next;
        xSemaphoreGive(state_mutex_);
    }

    // Re-apply the brightness for the current power state so a settings change
    // is visible immediately without waking a deliberately dark screen.
    const PowerState state = static_cast<PowerState>(power_state_.load());
    if (state == PowerState::Active) {
        apply_backlight(next.brightness_active, true);
    } else if (state == PowerState::Idle) {
        apply_backlight(next.brightness_idle, true);
    }

    ESP_LOGI(kTag,
             "display settings applied: rotation %d, active %d%%, idle %d%%, dim %ds, off %ds",
             next.rotation, next.brightness_active, next.brightness_idle, next.dim_timeout_s,
             next.screen_off_timeout_s);
    return Result<Unit>::ok(Unit{});
}

void EspDisplay::apply_rotation_locked(int rotation) {
    if (lv_display_ == nullptr) {
        return;
    }
    lv_display_set_rotation(lv_display_, to_lvgl_rotation(rotation));
}

bool EspDisplay::lock(uint32_t timeout_ms) {
    if (!lvgl_ready_.load()) {
        return false;
    }
    return lvgl_port_lock(timeout_ms);
}

void EspDisplay::unlock() {
    lvgl_port_unlock();
}

Result<Unit> EspDisplay::init_lvgl() {
    if (lvgl_ready_.load()) {
        return Result<Unit>::ok(Unit{});
    }
    if (panel_ == nullptr) {
        return Result<Unit>::fail("board/not_initialized", "panel must be initialized first");
    }

    lvgl_port_cfg_t port_config = {};
    port_config.task_priority = 4;
    // A page-1 rebuild allocates every member tile at once: 15 members leave
    // ~6 KiB of headroom, and the target deployment is 30+ members.
    port_config.task_stack = 24576;
    port_config.task_affinity = -1;
    port_config.task_max_sleep_ms = 500;
    port_config.task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT;
    port_config.timer_period_ms = 5;

    esp_err_t err = lvgl_port_init(&port_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "LVGL port init failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("board/lvgl", "LVGL port initialization failed");
    }

    lvgl_port_display_cfg_t display_config = {};
    display_config.panel_handle = panel_;
    // Partial draw buffer in *internal* RAM: rasterising into the PSRAM
    // framebuffer measured ~150 ms per 480x480 frame (memory-bound: unchanged by
    // -O2, by 240 MHz, by IRAM-resident drawing code), which capped the UI at
    // 5-8 fps. Drawing into SRAM and letting the port copy each band into the
    // framebuffer moves the cost to one bulk copy per band. 80 lines = 76.8 KiB,
    // affordable next to the 155 KiB internal heap; more lines means fewer
    // copies per frame.
    display_config.buffer_size = static_cast<uint32_t>(kWidth) * 80;
    display_config.double_buffer = false;
    display_config.hres = kWidth;
    display_config.vres = kHeight;
    display_config.monochrome = false;
    display_config.color_format = LV_COLOR_FORMAT_RGB565;
    display_config.rotation.swap_xy = false;
    display_config.rotation.mirror_x = false;
    display_config.rotation.mirror_y = false;
    // buff_spiram = false keeps the draw buffer in internal RAM (that is the
    // point of this configuration); buff_dma = true asks for a DMA-capable one.
    display_config.flags.buff_dma = true;
    display_config.flags.buff_spiram = false;
    // Rotation is fixed at 0 on this panel; three routes are dead ends:
    //  * sw_rotate + direct_mode: the port rotates ww x hh from the framebuffer
    //    base pointer (esp_lvgl_port 2.9.0 documents the pair as unsupported),
    //  * esp_lcd RGB swap_xy: a no-op once the draw buffer is a panel
    //    framebuffer, because the rotate mask only feeds the CPU-copy branch,
    //  * LVGL matrix rotation: not implemented by the software renderer.
    // Turning sw_rotate off therefore silences the broken path and drops the
    // third full-screen buffer. T-011 tracks a rotated flush that writes strips
    // into the back buffer. Non-zero rotation is rejected by the API and the
    // serial CLI, and clamped here for settings stored by older firmware.
    display_config.flags.sw_rotate = false;
    display_config.flags.swap_bytes = false;
    // Partial rendering: LVGL redraws only the invalidated bands into the SRAM
    // buffer. direct_mode/full_refresh would force either the PSRAM framebuffer
    // (slow, see buffer_size above) or a full redraw per frame.
    display_config.flags.full_refresh = false;
    display_config.flags.direct_mode = false;

    lvgl_port_display_rgb_cfg_t rgb_config = {};
    rgb_config.flags.bb_mode = false;
    // avoid_tearing hands LVGL the panel framebuffers, which is exactly what
    // forced the PSRAM rasterisation above. With the SRAM draw buffer the port
    // copies bands into the framebuffer instead; the trade-off is that a band
    // can be shown while the next one is being copied (tearing on animations).
    rgb_config.flags.avoid_tearing = false;

    lv_display_ = lvgl_port_add_disp_rgb(&display_config, &rgb_config);
    if (lv_display_ == nullptr) {
        ESP_LOGE(kTag, "LVGL RGB display registration failed");
        lvgl_port_deinit();
        return Result<Unit>::fail("board/lvgl_display", "LVGL display registration failed");
    }
    lvgl_ready_.store(true);

    if (!lvgl_port_lock(0)) {
        return Result<Unit>::fail("board/lvgl_lock", "cannot lock LVGL during initialisation");
    }

    lv_indev_ = lv_indev_create();
    if (lv_indev_ == nullptr) {
        lvgl_port_unlock();
        return Result<Unit>::fail("board/lvgl_indev", "LVGL input device creation failed");
    }
    lv_indev_set_type(lv_indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lv_indev_, &EspDisplay::touch_read_cb);
    lv_indev_set_user_data(lv_indev_, this);
    lv_indev_set_display(lv_indev_, lv_display_);
    // Poll the real indev from an explicit timer (the port's implicit timer is
    // not serviced reliably here). This period is also the upper bound on the
    // scroll sample rate, because each poll is what turns a drag into a new
    // frame: 30 ms capped the UI at ~33 fps, so poll at 12 ms (LVGL's own default
    // is 30 ms, but the renderer now takes ~3 ms per band).
    indev_timer_ = lv_timer_create(&EspDisplay::indev_poll_cb, 12, this);
    // Measure the UI frame rate: LVGL reports FLUSH_FINISH once per flushed
    // frame, i.e. exactly what a user perceives as the update rate.
    // Measure the UI frame rate. esp_lvgl_port owns the flush callback and does
    // not deliver LV_EVENT_FLUSH_FINISH on this path, so wrap it: count frames
    // and time each one. The port waits for VSYNC inside the flush, so the
    // duration separates a panel-bound from a draw-bound picture.
    g_flush_self = this;
    // Two panel framebuffers, blitted from the internal-RAM draw buffer and
    // swapped at VSYNC: fast rasterisation (SRAM) without visible tearing.
    if (esp_lcd_rgb_panel_get_frame_buffer(panel_, 2, &panel_fbs_[0], &panel_fbs_[1]) != ESP_OK) {
        return Result<Unit>::fail("board/rgb_fbs", "cannot obtain RGB frame buffers");
    }
    vsync_sem_ = xSemaphoreCreateCounting(1, 0);
    if (vsync_sem_ == nullptr) {
        return Result<Unit>::fail("board/vsync_sem", "cannot create VSYNC semaphore");
    }
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
    callbacks.on_vsync = &EspDisplay::vsync_cb;
    if (esp_lcd_rgb_panel_register_event_callbacks(panel_, &callbacks, this) != ESP_OK) {
        return Result<Unit>::fail("board/vsync_cb", "cannot register the VSYNC callback");
    }
    lv_display_set_flush_cb(lv_display_, &EspDisplay::tearfree_flush_cb);
    // Time the render pass separately from the flush: LVGL draws into the
    // framebuffer, then the port's flush waits for VSYNC. Only the split tells
    // whether a slow scroll is drawing-bound or panel-bound.
    lv_display_add_event_cb(lv_display_, &EspDisplay::render_start_cb, LV_EVENT_RENDER_START, this);
    lv_display_add_event_cb(lv_display_, &EspDisplay::render_ready_cb, LV_EVENT_RENDER_READY, this);

    apply_rotation_locked(0);
    rotation_.store(0);
    lvgl_port_unlock();

    ESP_LOGI(kTag, "LVGL port ready (%dx%d, rotation %d, software rotate)", kWidth, kHeight,
             settings_.rotation);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> EspDisplay::show_test_pattern() {
    if (!lvgl_ready_.load()) {
        return Result<Unit>::fail("board/lvgl_not_ready", "LVGL is not initialized");
    }
    wake();
    if (!lock(3000)) {
        return Result<Unit>::fail("board/display_busy", "LVGL did not yield for test pattern");
    }

    destroy_test_pattern_locked();

    test_root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(test_root_);
    lv_obj_set_size(test_root_, kWidth, kHeight);
    lv_obj_set_pos(test_root_, 0, 0);
    lv_obj_set_clickable(test_root_, false);
    lv_obj_set_scrollable(test_root_, false);
    lv_obj_set_style_bg_color(test_root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(test_root_, LV_OPA_50, 0);

    static lv_point_precise_t horizontal[] = {{0, kHeight / 2}, {kWidth - 1, kHeight / 2}};
    static lv_point_precise_t vertical[] = {{kWidth / 2, 0}, {kWidth / 2, kHeight - 1}};
    static lv_point_precise_t diagonal_a[] = {{0, 0}, {kWidth - 1, kHeight - 1}};
    static lv_point_precise_t diagonal_b[] = {{kWidth - 1, 0}, {0, kHeight - 1}};

    auto add_line = [this](const lv_point_precise_t* points, uint32_t count, lv_color_t color) {
        lv_obj_t* line = lv_line_create(test_root_);
        lv_obj_set_clickable(line, false);
        lv_line_set_points(line, points, count);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_color(line, color, 0);
        lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
    };
    add_line(horizontal, 2, lv_color_hex(0xFFFFFF));
    add_line(vertical, 2, lv_color_hex(0xFFFFFF));
    add_line(diagonal_a, 2, lv_color_hex(0x666666));
    add_line(diagonal_b, 2, lv_color_hex(0x666666));

    auto add_corner = [this](lv_align_t align, int x_ofs, int y_ofs, lv_color_t color,
                             const char* text, int text_x, int text_y, lv_align_t text_align) {
        lv_obj_t* box = lv_obj_create(test_root_);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, 30, 30);
        lv_obj_set_style_bg_color(box, color, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_align(box, align, x_ofs, y_ofs);
        lv_obj_set_clickable(box, false);
        lv_obj_set_scrollable(box, false);

        lv_obj_t* label = lv_label_create(test_root_);
        lv_label_set_text(label, text);
        lv_obj_set_clickable(label, false);
        lv_obj_set_style_text_color(label, color, 0);
        lv_obj_align(label, text_align, text_x, text_y);
    };

    add_corner(LV_ALIGN_TOP_LEFT, 4, 4, lv_color_hex(0xFF3B30), "0", 40, 8, LV_ALIGN_TOP_LEFT);
    add_corner(LV_ALIGN_TOP_RIGHT, -4, 4, lv_color_hex(0x34C759), "90", -40, 8, LV_ALIGN_TOP_RIGHT);
    add_corner(LV_ALIGN_BOTTOM_RIGHT, -4, -4, lv_color_hex(0x0A84FF), "180", -40, -8,
               LV_ALIGN_BOTTOM_RIGHT);
    add_corner(LV_ALIGN_BOTTOM_LEFT, 4, -4, lv_color_hex(0xFFD60A), "270", 40, -8,
               LV_ALIGN_BOTTOM_LEFT);

    test_label_ = lv_label_create(test_root_);
    lv_label_set_text_fmt(test_label_, "Rotation %d | Berührung -", rotation_.load());
    lv_obj_set_clickable(test_label_, false);
    lv_obj_set_style_text_color(test_label_, lv_color_white(), 0);
    lv_obj_align(test_label_, LV_ALIGN_CENTER, 0, 70);

    test_marker_ = lv_obj_create(test_root_);
    lv_obj_remove_style_all(test_marker_);
    lv_obj_set_size(test_marker_, 24, 24);
    lv_obj_set_style_radius(test_marker_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(test_marker_, lv_color_hex(0xFF00FF), 0);
    lv_obj_set_style_bg_opa(test_marker_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(test_marker_, lv_color_white(), 0);
    lv_obj_set_style_border_width(test_marker_, 2, 0);
    lv_obj_set_clickable(test_marker_, false);
    lv_obj_set_hidden(test_marker_, true);
    test_touch_active_ = false;

    // Touch feedback and automatic hide both run on the LVGL task.
    test_touch_timer_ = lv_timer_create(&EspDisplay::test_touch_timer_cb, 30, this);
    test_hide_timer_ = lv_timer_create(&EspDisplay::test_hide_timer_cb, 15000, this);
    lv_timer_set_repeat_count(test_hide_timer_, 1);

    unlock();
    ESP_LOGI(kTag, "display test pattern shown for 15 s (rotation %d)", rotation_.load());
    return Result<Unit>::ok(Unit{});
}

void EspDisplay::note_flush(int64_t duration_us) {
    flush_last_us_.store(static_cast<uint32_t>(duration_us));
    const int64_t now = esp_timer_get_time();
    if (flush_window_start_us_ == 0) {
        flush_window_start_us_ = now;
        flush_window_count_ = 1;
        return;
    }
    ++flush_window_count_;
    const int64_t elapsed = now - flush_window_start_us_;
    if (elapsed >= 1000000) {
        // fps * 1000: frames per second over the completed window.
        ui_fps_milli_.store(static_cast<uint32_t>(
            (static_cast<int64_t>(flush_window_count_) * 1000000000) / elapsed));
        flush_window_start_us_ = now;
        flush_window_count_ = 0;
    }
}

void EspDisplay::render_start_cb(lv_event_t* event) {
    auto* self = static_cast<EspDisplay*>(lv_event_get_user_data(event));
    if (self != nullptr) self->render_start_us_ = esp_timer_get_time();
}

void EspDisplay::render_ready_cb(lv_event_t* event) {
    auto* self = static_cast<EspDisplay*>(lv_event_get_user_data(event));
    if (self == nullptr || self->render_start_us_ == 0) return;
    self->render_last_us_.store(
        static_cast<uint32_t>(esp_timer_get_time() - self->render_start_us_));
}

bool IRAM_ATTR EspDisplay::vsync_cb(esp_lcd_panel_handle_t panel,
                                    const esp_lcd_rgb_panel_event_data_t* data, void* user_ctx) {
    auto* self = static_cast<EspDisplay*>(user_ctx);
    BaseType_t higher = pdFALSE;
    if (self != nullptr && self->vsync_sem_ != nullptr) {
        xSemaphoreGiveFromISR(self->vsync_sem_, &higher);
    }
    return higher == pdTRUE;
}

void EspDisplay::tearfree_flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    const int64_t started = esp_timer_get_time();
    auto* self = g_flush_self;
    if (self == nullptr || self->panel_fbs_[0] == nullptr) {
        lv_display_flush_ready(display);
        return;
    }

    // Blit the band from the SRAM draw buffer into the back framebuffer.
    auto* dst = static_cast<uint16_t*>(self->panel_fbs_[self->back_fb_]);
    const auto* src = reinterpret_cast<const uint16_t*>(pixels);
    const int width = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * kWidth + area->x1,
                    src + static_cast<size_t>(y - area->y1) * width,
                    static_cast<size_t>(width) * sizeof(uint16_t));
    }

    if (lv_display_flush_is_last(display)) {
        // Hand the finished framebuffer to the panel (the swap happens at the
        // next VSYNC), then wait one VSYNC so the *other* buffer is no longer
        // scanned and the next frame may write into it.
        esp_lcd_panel_draw_bitmap(self->panel_, 0, 0, kWidth, kHeight, dst);
        xSemaphoreTake(self->vsync_sem_, 0);
        xSemaphoreTake(self->vsync_sem_, pdMS_TO_TICKS(100));
        self->back_fb_ = 1 - self->back_fb_;
    }

    self->note_flush(esp_timer_get_time() - started);
    lv_display_flush_ready(display);
}

void EspDisplay::indev_poll_cb(lv_timer_t* timer) {
    auto* self = static_cast<EspDisplay*>(lv_timer_get_user_data(timer));
    if (self != nullptr && self->lv_indev_ != nullptr) {
        lv_indev_read(self->lv_indev_);
    }
}

void EspDisplay::test_touch_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<EspDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    self->update_test_touch();
}

void EspDisplay::test_hide_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<EspDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    // repeat_count == 1: LVGL deletes this timer after the callback returns.
    self->test_hide_timer_ = nullptr;
    if (self->test_touch_timer_ != nullptr) {
        lv_timer_delete(self->test_touch_timer_);
        self->test_touch_timer_ = nullptr;
    }
    if (self->test_root_ != nullptr) {
        lv_obj_delete(self->test_root_);
        self->test_root_ = nullptr;
        self->test_marker_ = nullptr;
        self->test_label_ = nullptr;
    }
    self->test_touch_active_ = false;
}

void EspDisplay::update_test_touch() {
    if (test_marker_ == nullptr || test_label_ == nullptr) {
        return;
    }
    const TouchSample sample = read_touch();
    if (sample.pressed) {
        // The snapshot is raw panel space; mirror LVGL's pointer rotation so
        // the test marker lands under the finger at every rotation.
        const int rotation = rotation_.load();
        const touch::Point logical = touch::clamp(
            touch::transform(sample.x, sample.y, kWidth, kHeight, rotation), kWidth, kHeight);
        if (!test_touch_active_) {
            lv_obj_set_hidden(test_marker_, false);
            test_touch_active_ = true;
        }
        lv_obj_set_pos(test_marker_, logical.x - 12, logical.y - 12);
        lv_label_set_text_fmt(test_label_, "Rotation %d | Berührung %d, %d", rotation,
                              static_cast<int>(logical.x), static_cast<int>(logical.y));
    } else if (test_touch_active_) {
        lv_obj_set_hidden(test_marker_, true);
        lv_label_set_text_fmt(test_label_, "Rotation %d | Berührung -", rotation_.load());
        test_touch_active_ = false;
    }
}

void EspDisplay::destroy_test_pattern_locked() {
    if (test_touch_timer_ != nullptr) {
        lv_timer_delete(test_touch_timer_);
        test_touch_timer_ = nullptr;
    }
    if (test_hide_timer_ != nullptr) {
        lv_timer_delete(test_hide_timer_);
        test_hide_timer_ = nullptr;
    }
    if (test_root_ != nullptr) {
        lv_obj_delete(test_root_);
        test_root_ = nullptr;
        test_marker_ = nullptr;
        test_label_ = nullptr;
    }
    test_touch_active_ = false;
}

bool EspDisplay::read_chip_temperature(float* celsius) {
    return read_temperature(celsius);
}

bool EspDisplay::read_temperature(float* celsius) const {
    if (celsius == nullptr || temperature_sensor_ == nullptr) {
        return false;
    }
    if (state_mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    float value = 0.0f;
    const bool ok = temperature_sensor_get_celsius(temperature_sensor_, &value) == ESP_OK;
    xSemaphoreGive(state_mutex_);
    if (!ok) {
        return false;
    }
    *celsius = value;
    return true;
}

uint64_t EspDisplay::uptime_seconds() const {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;
}

BoardStats EspDisplay::stats() const {
    BoardStats result;
    result.uptime_s = uptime_seconds();
    result.free_heap = esp_get_free_heap_size();
    result.min_free_heap = esp_get_minimum_free_heap_size();
    result.free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    result.total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    result.touch_present = touch_present_.load();
    result.rotation = rotation_.load();
    result.backlight_percent = current_percent_.load();
    result.temperature_valid = read_temperature(&result.chip_temperature_c);
    return result;
}

bool EspDisplay::touch_available() const {
    return touch_present_.load();
}

}  // namespace kaffee
