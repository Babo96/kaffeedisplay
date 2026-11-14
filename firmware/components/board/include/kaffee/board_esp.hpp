#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "driver/i2c_master.h"
#include "driver/temperature_sensor.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "kaffee/board.hpp"
#include "lvgl.h"

namespace kaffee {

// Live diagnostics exposed by the board component. The service layer copies
// these into SystemStats; the UI only reads them.
struct BoardStats {
    bool temperature_valid = false;
    float chip_temperature_c = 0.0f;
    uint64_t uptime_s = 0;
    uint32_t free_heap = 0;
    uint32_t min_free_heap = 0;
    uint32_t free_psram = 0;
    uint32_t total_psram = 0;
    bool touch_present = false;
    int rotation = 0;
    int backlight_percent = 0;
};

struct RgbTiming {
    int pclk_hz = 0;
    int hsync_pulse_width = 0;
    int hsync_back_porch = 0;
    int hsync_front_porch = 0;
    int vsync_pulse_width = 0;
    int vsync_back_porch = 0;
    int vsync_front_porch = 0;
};

// ESP-IDF implementation of IDisplay for the Guition ESP32-4848S040.
//
// init() sets up the ST7701S RGB panel, GT911 and the LEDC backlight. LVGL is
// initialised separately by init_lvgl(), which keeps the order in main.cpp
// explicit. Every LVGL call made by this class happens while the
// esp_lvgl_port mutex is held, so apply_settings() and show_test_pattern() are
// safe to call from the service/HTTP task.
class EspDisplay final : public IDisplay {
  public:
    static Result<std::unique_ptr<EspDisplay>> create();
    ~EspDisplay() override;

    EspDisplay(const EspDisplay&) = delete;
    EspDisplay& operator=(const EspDisplay&) = delete;

    Result<Unit> init() override;
    Result<Unit> init_lvgl();

    DisplayInfo info() const override;
    DisplaySettings settings() const override;
    RgbTiming rgb_timing() const;
    Result<Unit> apply_settings(const DisplaySettings& settings) override;
    Result<Unit> show_test_pattern() override;
    void set_backlight_percent(int percent, bool fade) override;
    void wake() override;
    bool read_chip_temperature(float* celsius) override;
    uint64_t uptime_seconds() const override;

    BoardStats stats() const;
    bool touch_available() const;

    // Keeps the panel awake while an AP/QR session is active. The UI/net layer
    // should call this with true when the AP starts and false when it stops.
    void set_stay_awake(bool stay_awake);
    // Releases the frame buffer after a VSYNC; called from the panel ISR.
    void on_vsync_from_isr();
    // Inject a synthetic press into the real LVGL indev read callback. The next
    // callback returns PRESSED, the following one RELEASED, so LVGL runs its
    // normal hit-test/click path. Coordinates are raw panel coordinates.
    bool inject_touch(int x, int y);
    // Inject a real press-drag-release gesture through the LVGL indev callback.
    bool inject_swipe(int x1, int y1, int x2, int y2);

    // Direct framebuffer colour test used for bring-up diagnostics. It does
    // not touch LVGL and leaves eight vertical colour bars on the panel.
    Result<Unit> run_scanout_selftest();

    lv_display_t* lv_display() const {
        return lv_display_;
    }
    bool lock(uint32_t timeout_ms = 0);
    void unlock();

  private:
    EspDisplay() = default;

    struct TouchSample {
        uint16_t x = 0;
        uint16_t y = 0;
        bool pressed = false;
    };

    enum class PowerState : int { Active = 0, Idle = 1, Off = 2 };

    Result<Unit> init_panel();
    Result<Unit> init_backlight();
    Result<Unit> init_touch();
    Result<Unit> recover_touch();
    Result<Unit> init_temperature_sensor();
    void start_tasks();
    void stop_tasks();

    static void touch_task_entry(void* arg);
    static void power_task_entry(void* arg);
    void touch_loop();
    void power_loop();

    static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    static void test_touch_timer_cb(lv_timer_t* timer);
    static void indev_poll_cb(lv_timer_t* timer);
    // Wraps esp_lvgl_port's flush callback to count and time produced frames.
    // Blits each LVGL band from the internal-RAM draw buffer into the back panel
    // framebuffer and swaps framebuffers at VSYNC, so drawing stays fast (SRAM)
    // and the panel never shows a half-drawn frame.
    static void tearfree_flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* pixels);
    void note_flush(int64_t duration_us);
    static void render_start_cb(lv_event_t* event);
    static void render_ready_cb(lv_event_t* event);
    static void test_hide_timer_cb(lv_timer_t* timer);

    void publish_touch(int32_t x, int32_t y, bool pressed);
    TouchSample read_touch() const;
    bool read_temperature(float* celsius) const;
    void note_activity();
    void set_power_state(PowerState state, bool fade);
    void apply_backlight(int percent, bool fade);
    uint32_t percent_to_duty(int percent) const;
    DisplaySettings snapshot_settings() const;
    void apply_rotation_locked(int rotation);
    void destroy_test_pattern_locked();
    void update_test_touch();

    static constexpr int kWidth = 480;
    static constexpr int kHeight = 480;
    static constexpr int kTouchPollMs = 10;  // 100 Hz
    static constexpr int kPowerTickMs = 200;
    static constexpr int kBacklightFadeMs = 250;

    esp_lcd_panel_handle_t panel_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;
    esp_lcd_panel_io_handle_t touch_io_ = nullptr;
    esp_lcd_touch_io_gt911_config_t gt911_driver_cfg_{};
    temperature_sensor_handle_t temperature_sensor_ = nullptr;
    bool fade_available_ = false;

    lv_display_t* lv_display_ = nullptr;
    lv_indev_t* lv_indev_ = nullptr;
    lv_timer_t* indev_timer_ = nullptr;
    // UI frame-rate sampling (LVGL task only, plus atomic published values).
    uint32_t flush_window_count_ = 0;
    int64_t flush_window_start_us_ = 0;
    std::atomic<uint32_t> ui_fps_milli_{0};
    std::atomic<uint32_t> flush_last_us_{0};
    // Double buffering for tearfree_flush_cb: the FB currently being scanned out
    // and the one we blit into.
    void* panel_fbs_[2] = {nullptr, nullptr};
    int back_fb_ = 1;
    bool frame_in_progress_ = false;
    SemaphoreHandle_t vsync_sem_ = nullptr;
    // Duration of the last LVGL render pass (drawing, excluding the flush).
    int64_t render_start_us_ = 0;
    std::atomic<uint32_t> render_last_us_{0};
    mutable SemaphoreHandle_t state_mutex_ = nullptr;
    SemaphoreHandle_t backlight_mutex_ = nullptr;
    SemaphoreHandle_t activity_mutex_ = nullptr;
    mutable portMUX_TYPE touch_lock_ = portMUX_INITIALIZER_UNLOCKED;
    TouchSample touch_sample_{};

    DisplaySettings settings_{};
    std::atomic<int> current_percent_{0};
    int backlight_target_percent_ = 0;
    bool test_touch_active_ = false;

    std::atomic<int> rotation_{0};
    std::atomic<int> power_state_{static_cast<int>(PowerState::Off)};
    std::atomic<bool> lvgl_ready_{false};
    std::atomic<bool> touch_present_{false};
    std::atomic<bool> synthetic_touch_pending_{false};
    std::atomic<uint16_t> synthetic_touch_x_{0};
    std::atomic<uint16_t> synthetic_touch_y_{0};
    std::atomic<bool> synthetic_swipe_active_{false};
    std::atomic<int> synthetic_swipe_step_{0};
    std::atomic<int> synthetic_swipe_steps_{0};
    std::atomic<int> synthetic_swipe_x0_{0};
    std::atomic<int> synthetic_swipe_y0_{0};
    std::atomic<int> synthetic_swipe_x1_{0};
    std::atomic<int> synthetic_swipe_y1_{0};
    std::atomic<bool> running_{false};
    bool stay_awake_ = false;
    std::atomic<int> task_count_{0};
    uint32_t last_activity_ms_ = 0;

    TaskHandle_t touch_task_ = nullptr;
    TaskHandle_t power_task_ = nullptr;

    lv_obj_t* test_root_ = nullptr;
    lv_obj_t* test_marker_ = nullptr;
    lv_obj_t* test_label_ = nullptr;
    lv_timer_t* test_hide_timer_ = nullptr;
    lv_timer_t* test_touch_timer_ = nullptr;
};

}  // namespace kaffee
