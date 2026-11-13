#include "serial_console.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "kaffee/board_esp.hpp"
#include "kaffee/ui_control.hpp"

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nvs.h"

namespace kaffee {
namespace {

constexpr const char* kTag = "kaffee.serial";
constexpr uart_port_t kPort = UART_NUM_0;
constexpr int kRxBuffer = 4096;
constexpr size_t kMaxLine = 512;
constexpr size_t kMaxBackupBytes = 256 * 1024;
constexpr uint32_t kUnlockWindowMs = 10 * 60 * 1000;
constexpr int64_t kMinPlausibleEpoch = 1600000000LL;  // 2020-09-13
constexpr int64_t kMaxPlausibleEpoch = 4102444800LL;  // 2100-01-01

std::string lower_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

std::string sanitize(std::string text, size_t limit = 160) {
    for (char& c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f) c = ' ';
    }
    if (text.size() > limit) text.resize(limit);
    return text;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        const size_t start = pos;
        while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') ++pos;
        if (pos > start) tokens.push_back(line.substr(start, pos - start));
    }
    return tokens;
}

std::string join(const std::vector<std::string>& tokens, size_t from) {
    std::string out;
    for (size_t i = from; i < tokens.size(); ++i) {
        if (!out.empty()) out.push_back(' ');
        out += tokens[i];
    }
    return out;
}

bool parse_i64(const std::string& text, int64_t& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    value = static_cast<int64_t>(parsed);
    return true;
}

const char* time_source_name(const TimeInfo& time) {
    if (!time.last_source.empty()) return time.last_source.c_str();
    switch (time.source) {
        case TimeSource::Synced:
            return "synced";
        case TimeSource::Restored:
            return "restored";
        case TimeSource::Unset:
            return "unset";
    }
    return "unset";
}

}  // namespace

class SerialConsole {
  public:
    SerialConsole(KaffeeService& service, IPlatform& platform, EspDisplay& display,
                  ui::UiControl* ui_control)
        : service_(service), platform_(platform), display_(display), ui_control_(ui_control) {}

    void set_ui_control(ui::UiControl* ui_control) {
        ui_control_.store(ui_control);
    }

    void run() {
        if (!install_uart()) return;
        ESP_LOGI(kTag, "serial console ready (115200 8N1, 'help' for commands)");
        handle_lines();
    }

  private:
    bool install_uart() {
        const esp_err_t err = uart_driver_install(kPort, kRxBuffer, 0, 0, nullptr, 0);
        if (err == ESP_ERR_INVALID_STATE) {
            // The console/log path may already own the driver; just reuse it.
            ESP_LOGW(kTag, "UART0 driver already installed; reusing it");
            return true;
        }
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "cannot install UART0 driver: %s", esp_err_to_name(err));
            return false;
        }
        uart_config_t config = {};
        config.baud_rate = 115200;
        config.data_bits = UART_DATA_8_BITS;
        config.parity = UART_PARITY_DISABLE;
        config.stop_bits = UART_STOP_BITS_1;
        config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        config.source_clk = UART_SCLK_DEFAULT;
        const esp_err_t configured = uart_param_config(kPort, &config);
        if (configured != ESP_OK) {
            ESP_LOGE(kTag, "cannot configure UART0: %s", esp_err_to_name(configured));
            return false;
        }
        return true;
    }

    void handle_lines() {
        std::string line;
        bool discard = false;
        uint8_t byte = 0;
        for (;;) {
            const int read = uart_read_bytes(kPort, &byte, 1, pdMS_TO_TICKS(100));
            if (read != 1) {
                if (reset_pending_ &&
                    static_cast<int32_t>(reset_deadline_ - xTaskGetTickCount()) <= 0) {
                    reset_pending_ = false;
                    reply_error("factory-reset", "timeout; nothing erased");
                }
                continue;
            }

            if (byte == '\r') continue;
            if (byte == '\n') {
                if (!discard) {
                    if (reset_pending_) {
                        reset_pending_ = false;
                        if (lower_ascii(line) == "yes") {
                            perform_factory_reset();
                            return;
                        }
                        reply_error("factory-reset", "cancelled");
                    } else if (!line.empty()) {
                        handle(line);
                    }
                }
                line.clear();
                discard = false;
                continue;
            }
            if (discard) continue;
            if (line.size() >= kMaxLine) {
                // Bounded reader: drop an overlong line up to the next newline
                // instead of growing the buffer.
                discard = true;
                line.clear();
                continue;
            }
            line.push_back(static_cast<char>(byte));
        }
    }

    void write_raw(const std::string& text) {
        size_t offset = 0;
        while (offset < text.size()) {
            const size_t chunk = std::min<size_t>(512, text.size() - offset);
            const int written =
                uart_write_bytes(kPort, text.data() + offset, static_cast<size_t>(chunk));
            if (written <= 0) return;
            offset += static_cast<size_t>(written);
        }
    }

    void reply(const char* format, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        const int length = std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (length < 0) return;
        const size_t size = std::min(static_cast<size_t>(length), sizeof(buffer) - 1);
        // Keep the line and CRLF in one write so an ESP-IDF log cannot land
        // between them and corrupt the machine-readable reply.
        std::string line(buffer, size);
        line += "\r\n";
        write_raw(line);
    }

    void reply_error(const std::string& code, const std::string& message) {
        reply("ERR %s %s", sanitize(code, 40).c_str(), sanitize(message).c_str());
    }

    std::string new_request_id() {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "cli_%016llx",
                      static_cast<unsigned long long>(platform_.random_u64()));
        return buffer;
    }

    // Verifies the current admin password through the normal auth path. The
    // password is never logged and the temporary session is dropped at once.
    bool verify_admin_password(const std::string& password, std::string& error_code,
                               std::string& error_message) {
        if (password.empty()) {
            error_code = "auth";
            error_message = "admin password required";
            return false;
        }
        auto session = service_.login("admin", password, "serial-cli");
        if (!session) {
            error_code = session.error ? session.error->code : "auth";
            error_message = session.error ? session.error->message : "invalid credentials";
            return false;
        }
        service_.logout(session.value->token);
        return true;
    }

    bool writes_allowed() const {
#if defined(KAFFEE_SERIAL_ALLOW_WRITES) && KAFFEE_SERIAL_ALLOW_WRITES
        return true;
#else
        return writes_until_ != 0 && static_cast<int32_t>(writes_until_ - xTaskGetTickCount()) > 0;
#endif
    }

    void handle(const std::string& line) {
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) return;

        // Only the command word is case-insensitive; arguments (passwords,
        // names, reasons) must keep their exact case.
        const std::string command = lower_ascii(tokens[0]);
        if (command == "help") return cmd_help();
        if (command == "status") return cmd_status();
        if (command == "ap") return cmd_ap(tokens);
        if (command == "setup-password") return cmd_setup_password();
        if (command == "time") return cmd_time(tokens);
        if (command == "display") return cmd_display(tokens);
        if (command == "settings") return cmd_settings(tokens);
        if (command == "ui") return cmd_ui(tokens);
        if (command == "book") return cmd_book(tokens);
        if (command == "void") return cmd_void(tokens);
        if (command == "unlock-writes") return cmd_unlock_writes(tokens);
        if (command == "backup") return cmd_backup();
        if (command == "factory-reset") return cmd_factory_reset(tokens);
        if (command == "reboot") return cmd_reboot();
        reply_error("unknown-command", command);
    }

    void cmd_help() {
        reply(
            "OK help commands=help,status,ap,setup-password,time,display,settings,ui,book,"
            "void,unlock-writes,backup,factory-reset,reboot");
    }

    void cmd_status() {
        const SystemStats stats = service_.stats();
        const std::vector<Event> events = service_.events();
        const std::vector<User> users = service_.users();
        const std::vector<Product> products = service_.products();
        const uint64_t seq = events.empty() ? 0 : events.back().seq;
        int active_users = 0;
        for (const User& user : users) {
            if (user.active) ++active_users;
        }
        int active_products = 0;
        for (const Product& product : products) {
            if (product.active) ++active_products;
        }
        const NetworkStatus& network = stats.network;
        const char* ap = network.ap_active ? "on" : (network.ap_starting ? "starting" : "off");
        const char* sta = network.sta_connected ? "on" : "off";
        const char* firmware =
            stats.firmware_version.empty() ? "unknown" : stats.firmware_version.c_str();
        const uint32_t psram_total =
            static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        const uint32_t internal_free =
            static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        const uint32_t internal_min =
            static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        const uint32_t internal_largest =
            static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        TaskHandle_t lvgl_task = xTaskGetHandle("taskLVGL");
        const uint32_t lvgl_stack_free =
            lvgl_task != nullptr ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(lvgl_task))
                                 : 0;
        TaskHandle_t httpd_task = xTaskGetHandle("httpd");
        const uint32_t httpd_stack_free =
            httpd_task != nullptr ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(httpd_task))
                                  : 0;

        reply(
            "OK status seq=%llu users=%u users_active=%d products=%u products_active=%d "
            "time=%lld time_source=%s time_trusted=%d ap=%s ap_ip=%s sta=%s sta_ip=%s "
            "uptime_s=%llu heap_free=%u heap_min=%u heap_internal_free=%u "
            "heap_internal_min=%u heap_internal_largest=%u psram_free=%u psram_total=%u "
            "lvgl_stack_free=%u httpd_stack_free=%u storage_used=%llu storage_total=%llu "
            "storage_mounted=%d firmware=%s drinks_today=%d",
            static_cast<unsigned long long>(seq), static_cast<unsigned>(users.size()), active_users,
            static_cast<unsigned>(products.size()), active_products,
            static_cast<long long>(stats.time.now), time_source_name(stats.time),
            stats.time.trusted ? 1 : 0, ap, network.ap_ip.c_str(), sta, network.sta_ip.c_str(),
            static_cast<unsigned long long>(stats.uptime_s), static_cast<unsigned>(stats.free_heap),
            static_cast<unsigned>(stats.min_free_heap), internal_free, internal_min,
            internal_largest, static_cast<unsigned>(stats.free_psram),
            static_cast<unsigned>(psram_total), lvgl_stack_free, httpd_stack_free,
            static_cast<unsigned long long>(stats.storage.used_bytes),
            static_cast<unsigned long long>(stats.storage.total_bytes),
            stats.storage.mounted ? 1 : 0, firmware, stats.drinks_today);
    }

    void cmd_ap(const std::vector<std::string>& tokens) {
        if (tokens.size() == 1) {
            const NetworkStatus status = service_.network_status();
            const char* state = status.ap_active ? "on" : (status.ap_starting ? "starting" : "off");
            if (status.ap_active) {
                // The password is only exposed while the AP is actually up.
                reply("OK ap state=%s ssid=%s password=%s ip=%s remaining_s=%d sta=%s", state,
                      status.ap_ssid.c_str(), status.ap_password.c_str(), status.ap_ip.c_str(),
                      status.ap_remaining_s, status.sta_connected ? "on" : "off");
            } else {
                reply("OK ap state=%s ssid=%s ip=%s remaining_s=%d sta=%s", state,
                      status.ap_ssid.c_str(), status.ap_ip.c_str(), status.ap_remaining_s,
                      status.sta_connected ? "on" : "off");
            }
            return;
        }

        if (tokens[1] == "start") {
            auto started = service_.start_ap();
            if (!started) return reply_error(started.error->code, started.error->message);
            int ttl = service_.settings().network.ap_window_s;
            if (ttl <= 0) ttl = 900;
            reply("OK ap start ttl=%d", ttl);
            return;
        }
        if (tokens[1] == "stop") {
            auto stopped = service_.stop_ap();
            if (!stopped) return reply_error(stopped.error->code, stopped.error->message);
            reply("OK ap stop");
            return;
        }
        if (tokens[1] == "extend") {
            int64_t seconds = 900;
            if (tokens.size() >= 3 && !parse_i64(tokens[2], seconds)) {
                return reply_error("ap-usage", "extend needs whole seconds");
            }
            if (seconds < 1 || seconds > 86400) {
                return reply_error("ap-usage", "extend seconds must be 1..86400");
            }
            auto extended = service_.extend_ap(static_cast<int>(seconds));
            if (!extended) return reply_error(extended.error->code, extended.error->message);
            reply("OK ap extend ttl=%lld", static_cast<long long>(seconds));
            return;
        }
        reply_error("ap-usage", "ap [start|stop|extend <seconds>]");
    }

    void cmd_setup_password() {
        if (!service_.must_change_admin_password()) {
            return reply_error("setup-password", "already changed");
        }
        const std::string password = service_.setup_password();
        if (password.empty()) return reply_error("setup-password", "not available");
        reply("OK setup-password %s", password.c_str());
    }

    void cmd_time(const std::vector<std::string>& tokens) {
        if (tokens.size() == 1) {
            const TimeInfo time = service_.time_info();
            reply("OK time %lld source=%s trusted=%d last_sync=%lld",
                  static_cast<long long>(time.now), time_source_name(time), time.trusted ? 1 : 0,
                  static_cast<long long>(time.last_sync));
            return;
        }
        if (tokens[1] != "set" || tokens.size() < 3 || tokens.size() > 4) {
            return reply_error("time-usage", "time [set <epoch> [manual|browser|sntp]]");
        }
        int64_t epoch = 0;
        if (!parse_i64(tokens[2], epoch))
            return reply_error("time-usage", "epoch must be whole seconds");
        if (epoch < kMinPlausibleEpoch || epoch > kMaxPlausibleEpoch) {
            return reply_error("time-usage", "epoch outside plausible range");
        }
        std::string source = "manual";
        if (tokens.size() == 4) {
            source = tokens[3];
            if (source != "manual" && source != "browser" && source != "sntp") {
                return reply_error("time-usage", "source must be manual, browser or sntp");
            }
        }
        auto synced = service_.sync_time(epoch, source);
        if (!synced) return reply_error(synced.error->code, synced.error->message);
        reply("OK time set %lld", static_cast<long long>(epoch));
    }

    void cmd_display(const std::vector<std::string>& tokens) {
        if (tokens.size() == 1 || tokens[1] == "info") return cmd_display_info();
        if (tokens[1] == "test") {
            auto shown = platform_.show_display_test();
            if (!shown) return reply_error(shown.error->code, shown.error->message);
            reply("OK display test timeout_s=15");
            return;
        }
        if (tokens[1] == "rotation") {
            int64_t rotation = 0;
            if (tokens.size() != 3 || !parse_i64(tokens[2], rotation) ||
                (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)) {
                return reply_error("display-usage", "display rotation <0|90|180|270>");
            }
            // Only 0 is implemented on this panel; see T-011.
            if (rotation != 0) {
                return reply_error("rotation-unsupported",
                                   "non-zero rotation is not supported on this display (T-011)");
            }
            Settings settings = service_.settings();
            settings.display.rotation = static_cast<int>(rotation);
            auto saved = service_.save_settings(settings, new_request_id());
            if (!saved) return reply_error(saved.error->code, saved.error->message);
            reply("OK display rotation %lld", static_cast<long long>(rotation));
            return;
        }
        if (tokens[1] == "brightness") {
            int64_t brightness = 0;
            if (tokens.size() != 3 || !parse_i64(tokens[2], brightness) || brightness < 0 ||
                brightness > 100) {
                return reply_error("display-usage", "display brightness <0..100>");
            }
            if (brightness < 5) {
                // The persisted active brightness range starts at 5%; 0..4 is
                // a temporary diagnostic setting (0 turns the backlight off).
                display_.set_backlight_percent(static_cast<int>(brightness), false);
                reply("OK display brightness %lld temporary=1", static_cast<long long>(brightness));
                return;
            }
            Settings settings = service_.settings();
            settings.display.brightness_active = static_cast<int>(brightness);
            auto saved = service_.save_settings(settings, new_request_id());
            if (!saved) return reply_error(saved.error->code, saved.error->message);
            reply("OK display brightness %lld", static_cast<long long>(brightness));
            return;
        }
        reply_error("display-usage", "display [test|info|rotation <deg>|brightness <0..100>]");
    }

    // Booking-page ordering: read it, or set it with the write unlock. There is
    // no other headless way to change an app setting when the admin UI is not
    // reachable (no AP/STA), and it is the setting most worth verifying on HIL.
    void cmd_settings(const std::vector<std::string>& tokens) {
        if (tokens.size() == 3 && tokens[1] == "user-sort") {
            if (!writes_allowed()) {
                return reply_error("writes-disabled",
                                   "run 'unlock-writes <admin_password>' first (HIL only)");
            }
            const UserSortOrder order = user_sort_from_string(tokens[2], UserSortOrder::Usage);
            if (tokens[2] != "creation" && tokens[2] != "usage") {
                return reply_error("settings-usage", "settings user-sort <creation|usage>");
            }
            Settings settings = service_.settings();
            settings.app.user_sort = order;
            auto saved = service_.save_settings(settings, new_request_id());
            if (!saved) return reply_error(saved.error->code, saved.error->message);
            reply("OK settings user-sort %s", user_sort_to_string(order));
            return;
        }
        if (tokens.size() == 2 && tokens[1] == "user-sort") {
            reply("OK settings user-sort=%s",
                  user_sort_to_string(service_.app_settings().user_sort));
            return;
        }
        reply_error("settings-usage", "settings user-sort [<creation|usage>]");
    }

    void cmd_display_info() {
        const DisplayInfo info = display_.info();
        const DisplaySettings settings = service_.display_settings();
        const RgbTiming timing = display_.rgb_timing();
        reply(
            "OK display info rotation=%d brightness=%d active=%d idle=%d backlight=%d "
            "width=%d height=%d pclk_hz=%d hsync_pulse=%d hsync_back=%d hsync_front=%d "
            "vsync_pulse=%d vsync_back=%d vsync_front=%d ui_fps=%.1f panel_hz=%.1f",
            info.rotation, info.brightness_percent, settings.brightness_active,
            settings.brightness_idle, info.backlight_on ? 1 : 0, info.width, info.height,
            timing.pclk_hz, timing.hsync_pulse_width, timing.hsync_back_porch,
            timing.hsync_front_porch, timing.vsync_pulse_width, timing.vsync_back_porch,
            timing.vsync_front_porch, static_cast<double>(info.ui_fps),
            static_cast<double>(info.panel_hz));
    }

    struct UiBookRequest {
        ui::UiControl* ui = nullptr;
        std::string user_id;
        std::string product_id;
    };

    static void ui_book_async(void* argument) {
        std::unique_ptr<UiBookRequest> request(static_cast<UiBookRequest*>(argument));
        if (request == nullptr || request->ui == nullptr) return;
        request->ui->debug_select_user(request->user_id);
        request->ui->debug_book(request->product_id);
    }

    void cmd_ui(const std::vector<std::string>& tokens) {
        if (tokens.size() == 2 && tokens[1] == "page") {
            ui::UiControl* ui = ui_control_.load();
            if (ui == nullptr) return reply_error("ui-not-ready", "UI app is not running");
            reply("OK ui page %d", ui->page());
            return;
        }
        if (tokens.size() >= 2 &&
            (tokens[1] == "tap" || tokens[1] == "swipe" || tokens[1] == "book") &&
            !writes_allowed()) {
            return reply_error("writes-disabled",
                               "run 'unlock-writes <admin_password>' first (HIL only)");
        }
        if (tokens.size() >= 2 && tokens[1] == "tap") {
            int64_t x = 0;
            int64_t y = 0;
            if (tokens.size() != 4 || !parse_i64(tokens[2], x) || !parse_i64(tokens[3], y) ||
                x < 0 || x >= 480 || y < 0 || y >= 480) {
                return reply_error("ui-usage", "ui tap <x 0..479> <y 0..479>");
            }
            display_.inject_touch(static_cast<int>(x), static_cast<int>(y));
            reply("OK ui tap x=%lld y=%lld", static_cast<long long>(x), static_cast<long long>(y));
            return;
        }
        if (tokens.size() >= 2 && tokens[1] == "swipe") {
            int64_t x1 = 0;
            int64_t y1 = 0;
            int64_t x2 = 0;
            int64_t y2 = 0;
            if (tokens.size() != 6 || !parse_i64(tokens[2], x1) || !parse_i64(tokens[3], y1) ||
                !parse_i64(tokens[4], x2) || !parse_i64(tokens[5], y2) || x1 < 0 || x1 >= 480 ||
                y1 < 0 || y1 >= 480 || x2 < 0 || x2 >= 480 || y2 < 0 || y2 >= 480) {
                return reply_error("ui-usage", "ui swipe <x1> <y1> <x2> <y2> (0..479)");
            }
            if (!display_.inject_swipe(static_cast<int>(x1), static_cast<int>(y1),
                                       static_cast<int>(x2), static_cast<int>(y2))) {
                return reply_error("ui-not-ready", "LVGL input is not ready");
            }
            reply("OK ui swipe x1=%lld y1=%lld x2=%lld y2=%lld", static_cast<long long>(x1),
                  static_cast<long long>(y1), static_cast<long long>(x2),
                  static_cast<long long>(y2));
            return;
        }
        if (tokens.size() == 4 && tokens[1] == "book") {
            ui::UiControl* ui = ui_control_.load();
            if (ui == nullptr) return reply_error("ui-not-ready", "UI app is not running");
            auto request = std::make_unique<UiBookRequest>();
            request->ui = ui;
            request->user_id = tokens[2];
            request->product_id = tokens[3];
            if (!display_.lock(1000)) {
                return reply_error("display-busy", "LVGL did not yield");
            }
            UiBookRequest* raw = request.get();
            const lv_result_t queued = lv_async_call(&SerialConsole::ui_book_async, raw);
            if (queued != LV_RESULT_OK) {
                display_.unlock();
                return reply_error("ui-queue", "LVGL async queue is full");
            }
            request.release();
            display_.unlock();
            reply("OK ui book queued user=%s product=%s", tokens[2].c_str(), tokens[3].c_str());
            return;
        }
        reply_error("ui-usage",
                    "ui page | ui tap <x> <y> | ui swipe <x1> <y1> <x2> <y2> | "
                    "ui book <user_id> <product_id>");
    }

    void cmd_book(const std::vector<std::string>& tokens) {
        if (!writes_allowed()) {
            return reply_error("writes-disabled",
                               "run 'unlock-writes <admin_password>' first (HIL only)");
        }
        if (tokens.size() < 3 || tokens.size() > 4) {
            return reply_error("book-usage", "book <user_id> <product_id> [request_id]");
        }
        const std::string request_id = tokens.size() == 4 ? tokens[3] : new_request_id();
        auto booked = service_.purchase(tokens[1], tokens[2], request_id);
        if (!booked) return reply_error(booked.error->code, booked.error->message);
        reply("OK booking %s %lld", booked.value->id.c_str(),
              static_cast<long long>(booked.value->amount_cents));
    }

    void cmd_void(const std::vector<std::string>& tokens) {
        if (!writes_allowed()) {
            return reply_error("writes-disabled",
                               "run 'unlock-writes <admin_password>' first (HIL only)");
        }
        if (tokens.size() < 2) return reply_error("void-usage", "void <event_id> [reason]");
        std::string reason = join(tokens, 2);
        if (reason.empty()) reason = "serial";
        auto voided = service_.void_event(tokens[1], reason, new_request_id());
        if (!voided) return reply_error(voided.error->code, voided.error->message);
        reply("OK void %s", tokens[1].c_str());
    }

    void cmd_unlock_writes(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            return reply_error("auth", "usage: unlock-writes <admin_password>");
        }
        std::string code;
        std::string message;
        if (!verify_admin_password(join(tokens, 1), code, message)) {
            return reply_error(code, message);
        }
        writes_until_ = xTaskGetTickCount() + pdMS_TO_TICKS(kUnlockWindowMs);
        reply("OK unlock-writes ttl=%u", static_cast<unsigned>(kUnlockWindowMs / 1000));
    }

    void cmd_backup() {
        auto backup = service_.export_backup();
        if (!backup) return reply_error(backup.error->code, backup.error->message);
        if (backup.value->size() > kMaxBackupBytes) {
            return reply_error("backup", "too large for serial; use the admin API");
        }
        reply("OK backup %u", static_cast<unsigned>(backup.value->size()));
        write_raw(*backup.value);
        write_raw("\r\nEND backup\r\n");
        uart_wait_tx_done(kPort, pdMS_TO_TICKS(5000));
    }

    void cmd_factory_reset(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            return reply_error("auth", "usage: factory-reset <admin_password>");
        }
        std::string code;
        std::string message;
        if (!verify_admin_password(join(tokens, 1), code, message)) {
            return reply_error(code, message);
        }
        reset_pending_ = true;
        reset_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
        reply("OK factory-reset confirm=YES timeout_s=30");
    }

    void perform_factory_reset() {
        auto erased = service_.factory_reset();
        if (!erased) return reply_error(erased.error->code, erased.error->message);
        // Do not call NetworkManager::factory_reset_credentials() here: its
        // synchronous AP stop can deadlock against the Wi-Fi event task on this
        // board revision (tracked as a separate net bug). Erase the namespace
        // directly; the immediate reboot stops the AP and first boot generates
        // fresh AP credentials.
        if (!erase_network_credentials()) {
            ESP_LOGW(kTag, "network NVS erase failed; rebooting anyway");
        }
        reply("OK factory-reset");
        reboot_now();
    }

    bool erase_network_credentials() {
        nvs_handle_t handle = 0;
        esp_err_t err = nvs_open("kaffee_net", NVS_READWRITE, &handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) return true;
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "nvs_open(kaffee_net) failed: %s", esp_err_to_name(err));
            return false;
        }
        err = nvs_erase_all(handle);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "nvs erase(kaffee_net) failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    void cmd_reboot() {
        reply("OK reboot");
        reboot_now();
    }

    void reboot_now() {
        uart_wait_tx_done(kPort, pdMS_TO_TICKS(2000));
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
    }

    KaffeeService& service_;
    IPlatform& platform_;
    EspDisplay& display_;
    std::atomic<ui::UiControl*> ui_control_{nullptr};
    TickType_t writes_until_ = 0;
    bool reset_pending_ = false;
    TickType_t reset_deadline_ = 0;
};

namespace {

void serial_task(void* argument) {
    static_cast<SerialConsole*>(argument)->run();
}

}  // namespace

namespace {
SerialConsole* g_console = nullptr;
}

bool start_serial_console(KaffeeService& service, IPlatform& platform, EspDisplay& display,
                          ui::UiControl* ui_control) {
    static SerialConsole console(service, platform, display, ui_control);
    g_console = &console;
    // Start after the network but before the UI. Reserve the stack in .bss so
    // it is guaranteed internal, contiguous and available after flash-cache
    // operations. The UI control pointer is attached later.
    // ESP-IDF's xTaskCreateStatic() takes the depth in BYTES (unlike vanilla
    // FreeRTOS), so the array is sized in bytes and the depth is sizeof().
    static constexpr size_t kSerialStackBytes = 12288;
    static StaticTask_t task_buffer;
    static StackType_t task_stack[kSerialStackBytes / sizeof(StackType_t)];
    if (xTaskCreateStatic(serial_task, "serial_cli", sizeof(task_stack), &console, 3, task_stack,
                          &task_buffer) == nullptr) {
        ESP_LOGE(kTag, "cannot create serial console task");
        return false;
    }
    return true;
}

void set_serial_console_ui(ui::UiControl* ui_control) {
    if (g_console != nullptr) g_console->set_ui_control(ui_control);
}

}  // namespace kaffee
