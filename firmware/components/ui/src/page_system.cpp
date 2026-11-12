#include <cstdio>
#include <string>

#include "format.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_internal.hpp"

namespace kaffee::ui {

namespace {

std::string escape_wifi(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

lv_obj_t* make_info_row(lv_obj_t* parent, const char* name, const std::string& value,
                        lv_obj_t** value_label = nullptr,
                        lv_color_t value_color = lv_color_hex(0xF5EFE6)) {
    lv_obj_t* row = lv_obj_create(parent);
    theme::make_transparent(row);
    lv_obj_set_clickable(row, false);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 2, 0);
    theme::hide_scrollbars(row);

    theme::make_label(row, name, theme::font_small(), theme::color(theme::COLOR_MUTED));
    lv_obj_t* value_text = theme::make_label(row, value.c_str(), theme::font_body(), value_color);
    lv_obj_set_style_text_align(value_text, LV_TEXT_ALIGN_RIGHT, 0);
    if (value_label != nullptr) *value_label = value_text;
    return row;
}

lv_obj_t* make_qr_block(lv_obj_t* parent, const std::string& payload, const char* caption,
                        int size) {
    lv_obj_t* block = lv_obj_create(parent);
    theme::make_transparent(block);
    lv_obj_set_clickable(block, false);
    lv_obj_set_width(block, lv_pct(48));
    lv_obj_set_height(block, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(block, 4, 0);
    theme::hide_scrollbars(block);

    lv_obj_t* qr = lv_qrcode_create(block);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, payload.data(), static_cast<uint32_t>(payload.size()));
    lv_obj_center(qr);

    lv_obj_t* text =
        theme::make_label(block, caption, theme::font_small(), theme::color(theme::COLOR_MUTED));
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    return block;
}

}  // namespace

void UiAppImpl::build_page_system(lv_obj_t* tile) {
    build_header(tile, strings::SYSTEM_TITLE, &clock_labels_[2]);

    system_body_ = lv_obj_create(tile);
    lv_obj_set_size(system_body_, lv_pct(100), 424);
    lv_obj_set_pos(system_body_, 0, 56);
    theme::make_transparent(system_body_);
    lv_obj_set_flex_flow(system_body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(system_body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(system_body_, 16, 0);
    lv_obj_set_style_pad_right(system_body_, 16, 0);
    lv_obj_set_style_pad_top(system_body_, 8, 0);
    lv_obj_set_style_pad_bottom(system_body_, 44, 0);
    lv_obj_set_style_pad_row(system_body_, 10, 0);
    lv_obj_set_scroll_dir(system_body_, LV_DIR_VER);
    theme::hide_scrollbars(system_body_);

    refresh_system_page();
}

void UiAppImpl::refresh_system_page() {
    if (system_body_ == nullptr) return;
    NetworkStatus network = backend_.network_status();
    set_stay_awake(network.ap_active);
    TimeInfo time = backend_.time_info();
    DisplaySettings display = backend_.display_settings();
    const bool must_change = backend_.must_change_admin_password();
    bool display_changed = display.rotation != last_display_.rotation ||
                           display.brightness_active != last_display_.brightness_active ||
                           display.brightness_idle != last_display_.brightness_idle ||
                           display.dim_timeout_s != last_display_.dim_timeout_s ||
                           display.screen_off_timeout_s != last_display_.screen_off_timeout_s;
    if (wlan_status_label_ == nullptr || network.ap_active != last_ap_active_ ||
        network.sta_connected != last_sta_connected_ || time.trusted != last_time_trusted_ ||
        must_change != last_must_change_ || display_changed) {
        last_ap_active_ = network.ap_active;
        last_sta_connected_ = network.sta_connected;
        last_time_trusted_ = time.trusted;
        last_display_ = display;
        last_must_change_ = must_change;
        build_system_body();
    }
    update_system_dynamic();
}

void UiAppImpl::build_system_body() {
    lv_obj_clean(system_body_);
    ap_hold_button_ = nullptr;
    ap_hold_label_ = nullptr;
    wlan_status_label_ = nullptr;
    wlan_countdown_label_ = nullptr;
    system_time_big_ = nullptr;
    system_time_date_ = nullptr;
    system_time_source_ = nullptr;
    system_time_last_ = nullptr;
    system_time_quality_ = nullptr;
    system_time_warning_ = nullptr;
    for (lv_obj_t*& value : stat_values_) value = nullptr;

    NetworkStatus network = backend_.network_status();
    TimeInfo time = backend_.time_info();
    SystemStats stats = backend_.stats();
    DisplaySettings display = backend_.display_settings();

    // --- First setup (initial admin password) -----------------------------
    if (backend_.must_change_admin_password()) {
        lv_obj_t* setup = theme::make_card(system_body_);
        lv_obj_set_width(setup, lv_pct(100));
        lv_obj_set_height(setup, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(setup, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(setup, 4, 0);

        theme::make_label(setup, strings::SYSTEM_SETUP_TITLE, theme::font_title_bold(),
                          theme::color(theme::COLOR_ACCENT));
        lv_obj_t* hint = theme::make_label(setup, strings::SYSTEM_SETUP_HINT, theme::font_small(),
                                           theme::color(theme::COLOR_MUTED));
        theme::set_wrapped(hint);

        const std::string setup_password = backend_.setup_password();
        if (!setup_password.empty()) {
            make_info_row(setup, strings::SYSTEM_SETUP_PASSWORD, setup_password, nullptr,
                          theme::color(theme::COLOR_ACCENT));
        }
        if (!network.ap_ssid.empty()) {
            make_info_row(setup, strings::SYSTEM_SETUP_SSID, network.ap_ssid);
        }
        const std::string url =
            "http://" + (network.ap_ip.empty() ? std::string("192.168.4.1") : network.ap_ip) + "/";
        make_info_row(setup, strings::SYSTEM_SETUP_URL, url);
        if (!network.ap_active) {
            lv_obj_t* start_hint =
                theme::make_label(setup, strings::SYSTEM_SETUP_AP_HINT, theme::font_small(),
                                  theme::color(theme::COLOR_MUTED));
            theme::set_wrapped(start_hint);
        }
    }

    // --- WLAN -------------------------------------------------------------
    lv_obj_t* wlan = theme::make_card(system_body_);
    lv_obj_set_width(wlan, lv_pct(100));
    lv_obj_set_height(wlan, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wlan, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wlan, 6, 0);

    lv_obj_t* wlan_head = theme::make_row(wlan, 8);
    theme::make_label(wlan_head, strings::SYSTEM_WLAN, theme::font_title_bold(),
                      theme::color(theme::COLOR_TEXT));
    const char* status_text =
        network.ap_active
            ? strings::SYSTEM_WLAN_ACTIVE
            : (network.ap_starting ? strings::SYSTEM_WLAN_STARTING : strings::SYSTEM_WLAN_INACTIVE);
    lv_color_t status_color =
        network.ap_active ? theme::color(theme::COLOR_POSITIVE) : theme::color(theme::COLOR_MUTED);
    wlan_status_label_ =
        theme::make_label(wlan_head, status_text, theme::font_body_bold(), status_color);
    lv_obj_set_flex_grow(wlan_status_label_, 1);
    lv_obj_set_style_text_align(wlan_status_label_, LV_TEXT_ALIGN_RIGHT, 0);

    if (network.ap_active) {
        make_info_row(wlan, strings::SYSTEM_WLAN_SSID, network.ap_ssid);
        make_info_row(wlan, strings::SYSTEM_WLAN_PASSWORD, network.ap_password);
        make_info_row(wlan, strings::SYSTEM_WLAN_ADDRESS, network.ap_ip);
        make_info_row(wlan, strings::SYSTEM_WLAN_REMAINING,
                      format_countdown(network.ap_remaining_s), &wlan_countdown_label_,
                      theme::color(theme::COLOR_ACCENT));

        lv_obj_t* actions = theme::make_row(wlan, 8);
        lv_obj_set_style_pad_top(actions, 2, 0);
        lv_obj_t* extend = theme::make_button(actions, strings::SYSTEM_WLAN_EXTEND, false);
        lv_obj_set_flex_grow(extend, 1);
        lv_obj_add_event_cb(extend, &UiAppImpl::ap_extend_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* stop = theme::make_button(actions, strings::SYSTEM_WLAN_STOP, false);
        lv_obj_set_flex_grow(stop, 1);
        lv_obj_add_event_cb(stop, &UiAppImpl::ap_stop_cb, LV_EVENT_CLICKED, this);

        lv_obj_t* qr_row = theme::make_row(wlan, 8);
        lv_obj_set_style_pad_top(qr_row, 6, 0);
        std::string wifi = "WIFI:T:WPA;S:" + escape_wifi(network.ap_ssid) +
                           ";P:" + escape_wifi(network.ap_password) + ";;";
        std::string url = "http://" + network.ap_ip + "/";
        make_qr_block(qr_row, wifi, strings::SYSTEM_WLAN_JOIN_QR, 120);
        make_qr_block(qr_row, url, strings::SYSTEM_WLAN_URL_QR, 120);
    } else {
        lv_obj_t* help = theme::make_label(wlan, strings::SYSTEM_WLAN_HELP, theme::font_small(),
                                           theme::color(theme::COLOR_MUTED));
        theme::set_wrapped(help);

        if (network.sta_connected) {
            make_info_row(wlan, strings::SYSTEM_WLAN_LAN, network.sta_ip);
        }

        lv_obj_t* start = theme::make_button(wlan, strings::SYSTEM_WLAN_START, true);
        lv_obj_set_width(start, lv_pct(100));
        lv_obj_add_event_cb(start, &UiAppImpl::ap_hold_pressed_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(start, &UiAppImpl::ap_hold_released_cb, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(start, &UiAppImpl::ap_hold_released_cb, LV_EVENT_PRESS_LOST, this);
        ap_hold_button_ = start;
        ap_hold_label_ = lv_obj_get_child(start, 0);

        if (network.sta_connected && !network.sta_ip.empty()) {
            lv_obj_t* qr_row = theme::make_row(wlan, 8);
            lv_obj_set_style_pad_top(qr_row, 6, 0);
            make_qr_block(qr_row, "http://" + network.sta_ip + "/", strings::SYSTEM_WLAN_LAN_QR,
                          120);
        }
    }

    // --- Time -------------------------------------------------------------
    lv_obj_t* time_card = theme::make_card(system_body_);
    lv_obj_set_width(time_card, lv_pct(100));
    lv_obj_set_height(time_card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(time_card, 5, 0);

    theme::make_label(time_card, strings::SYSTEM_TIME, theme::font_title_bold(),
                      theme::color(theme::COLOR_TEXT));

    lv_obj_t* clock_row = theme::make_row(time_card, 10);
    system_time_big_ = theme::make_label(clock_row, "--:--", theme::font_huge_bold(),
                                         theme::color(theme::COLOR_TEXT));
    lv_obj_set_flex_grow(system_time_big_, 1);
    system_time_date_ =
        theme::make_label(clock_row, "", theme::font_body(), theme::color(theme::COLOR_MUTED));
    lv_obj_set_style_text_align(system_time_date_, LV_TEXT_ALIGN_RIGHT, 0);

    make_info_row(time_card, strings::SYSTEM_TIME_SOURCE, "", &system_time_source_);
    make_info_row(time_card, strings::SYSTEM_TIME_LAST_SYNC, "", &system_time_last_);
    make_info_row(time_card, strings::SYSTEM_TIME_QUALITY, "", &system_time_quality_);

    system_time_warning_ =
        theme::make_label(time_card, strings::SYSTEM_TIME_WARNING, theme::font_small(),
                          theme::color(theme::COLOR_NEGATIVE));
    theme::set_wrapped(system_time_warning_);
    lv_obj_set_hidden(system_time_warning_, true);

    lv_obj_t* sync = theme::make_button(time_card, strings::SYSTEM_TIME_SYNC, false);
    lv_obj_set_width(sync, lv_pct(100));
    lv_obj_add_event_cb(sync, &UiAppImpl::time_sync_cb, LV_EVENT_CLICKED, this);

    // --- Device -----------------------------------------------------------
    lv_obj_t* device = theme::make_card(system_body_);
    lv_obj_set_width(device, lv_pct(100));
    lv_obj_set_height(device, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(device, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(device, 1, 0);
    theme::make_label(device, strings::SYSTEM_DEVICE, theme::font_title_bold(),
                      theme::color(theme::COLOR_TEXT));
    make_info_row(device, strings::SYSTEM_TEMPERATURE, "", &stat_values_[STAT_TEMPERATURE]);
    make_info_row(device, strings::SYSTEM_UPTIME, "", &stat_values_[STAT_UPTIME]);
    make_info_row(device, strings::SYSTEM_HEAP, "", &stat_values_[STAT_HEAP]);
    make_info_row(device, strings::SYSTEM_PSRAM, "", &stat_values_[STAT_PSRAM]);
    make_info_row(device, strings::SYSTEM_STORAGE, "", &stat_values_[STAT_STORAGE]);
    make_info_row(device, strings::SYSTEM_FIRMWARE, "", &stat_values_[STAT_FIRMWARE]);
    make_info_row(device, strings::SYSTEM_MAC, "", &stat_values_[STAT_MAC]);
    make_info_row(device, strings::SYSTEM_IP, "", &stat_values_[STAT_NETWORK]);
    make_info_row(device, strings::SYSTEM_DRINKS_TODAY, "", &stat_values_[STAT_DRINKS_TODAY]);

    // --- Display ----------------------------------------------------------
    lv_obj_t* display_card = theme::make_card(system_body_);
    lv_obj_set_width(display_card, lv_pct(100));
    lv_obj_set_height(display_card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(display_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(display_card, 1, 0);
    theme::make_label(display_card, strings::SYSTEM_DISPLAY, theme::font_title_bold(),
                      theme::color(theme::COLOR_TEXT));
    make_info_row(display_card, strings::SYSTEM_ROTATION, rotation_label(display.rotation));
    make_info_row(display_card, strings::SYSTEM_BRIGHTNESS,
                  std::to_string(display.brightness_active) + strings::COMMON_PERCENT);
    make_info_row(display_card, strings::SYSTEM_DIM_TIMEOUT,
                  display.dim_timeout_s == 0
                      ? strings::SYSTEM_NEVER
                      : std::to_string(display.dim_timeout_s) + strings::UNIT_SECONDS);
    make_info_row(display_card, strings::SYSTEM_OFF_TIMEOUT,
                  display.screen_off_timeout_s == 0
                      ? strings::SYSTEM_NEVER
                      : std::to_string(display.screen_off_timeout_s) + strings::UNIT_SECONDS);
    lv_obj_t* note = theme::make_label(display_card, strings::SYSTEM_DISPLAY_READONLY,
                                       theme::font_small(), theme::color(theme::COLOR_DIM));
    theme::set_wrapped(note);

    (void)stats;
    (void)time;
}

void UiAppImpl::update_system_dynamic() {
    if (system_body_ == nullptr || current_page_ != 2) return;
    const uint32_t now_ms = lv_tick_get();
    if (last_system_dynamic_ms_ != 0 && now_ms - last_system_dynamic_ms_ < 5000) return;
    last_system_dynamic_ms_ = now_ms;
    NetworkStatus network = backend_.network_status();
    TimeInfo time = backend_.time_info();
    SystemStats stats = backend_.stats();

    if (wlan_countdown_label_ != nullptr) {
        lv_label_set_text(wlan_countdown_label_, format_countdown(network.ap_remaining_s).c_str());
    }

    if (system_time_big_ != nullptr) {
        lv_label_set_text(system_time_big_, format_clock(time.now).c_str());
        lv_label_set_text(system_time_date_, format_date(time.now).c_str());
        lv_label_set_text(system_time_source_,
                          time_source_label(time.source, time.last_source).c_str());
        lv_label_set_text(system_time_last_, format_last_sync(time.now, time.last_sync).c_str());
        lv_label_set_text(system_time_quality_, time.trusted ? strings::SYSTEM_TIME_TRUSTED
                                                             : strings::SYSTEM_TIME_UNTRUSTED);
        lv_obj_set_style_text_color(
            system_time_quality_,
            theme::color(time.trusted ? theme::COLOR_POSITIVE : theme::COLOR_NEGATIVE), 0);
        if (time.trusted) {
            lv_obj_set_hidden(system_time_warning_, true);
        } else {
            lv_obj_set_hidden(system_time_warning_, false);
        }
    }

    auto set_stat = [&](StatIndex index, const std::string& value) {
        if (stat_values_[index] != nullptr) {
            lv_label_set_text(stat_values_[index], value.c_str());
        }
    };

    set_stat(STAT_TEMPERATURE, stats.temperature_valid
                                   ? format_temperature(stats.chip_temperature_c)
                                   : strings::SYSTEM_NONE);
    set_stat(STAT_UPTIME, format_uptime(stats.uptime_s));
    set_stat(STAT_HEAP, format_bytes(stats.free_heap));
    set_stat(STAT_PSRAM,
             stats.free_psram > 0 ? format_bytes(stats.free_psram) : strings::SYSTEM_NONE);
    set_stat(STAT_STORAGE, format_bytes(stats.storage.used_bytes) + " / " +
                               format_bytes(stats.storage.total_bytes));
    set_stat(STAT_FIRMWARE,
             stats.firmware_version.empty() ? strings::SYSTEM_NONE : stats.firmware_version);
    set_stat(STAT_MAC, stats.mac.empty() ? strings::SYSTEM_NONE : stats.mac);
    std::string ip = strings::SYSTEM_NONE;
    if (stats.network.sta_connected && !stats.network.sta_ip.empty()) {
        ip = stats.network.sta_ip + " · " + std::to_string(stats.network.sta_rssi) + " dBm";
    } else if (stats.network.ap_active && !stats.network.ap_ip.empty()) {
        ip = stats.network.ap_ip;
    }
    set_stat(STAT_NETWORK, ip);
    set_stat(STAT_DRINKS_TODAY, std::to_string(stats.drinks_today));
}

void UiAppImpl::ap_hold_pressed_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    app->ap_hold_active_ = true;
    app->ap_hold_started_ = lv_tick_get();
    app->apply_hold_state();
}

void UiAppImpl::ap_hold_released_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    app->ap_hold_active_ = false;
    app->apply_hold_state();
}

void UiAppImpl::start_ap_from_ui() {
    Result<Unit> result = backend_.start_ap();
    if (!result) {
        show_toast(strings::SYSTEM_WLAN_FAILED, false, "", 3000);
        return;
    }
    queue_.push();
    show_toast(strings::SYSTEM_WLAN_AP_STARTED, false, "", 3000);
    refresh_system_page();
}

void UiAppImpl::stop_ap_from_ui() {
    Result<Unit> result = backend_.stop_ap();
    if (!result) {
        show_toast(strings::SYSTEM_WLAN_FAILED, false, "", 3000);
        return;
    }
    queue_.push();
    show_toast(strings::SYSTEM_WLAN_STOPPED, false, "", 3000);
    refresh_system_page();
}

void UiAppImpl::extend_ap_from_ui() {
    Result<Unit> result = backend_.extend_ap(300);
    if (!result) {
        show_toast(strings::SYSTEM_WLAN_FAILED, false, "", 3000);
        return;
    }
    refresh_system_page();
}

void UiAppImpl::sync_time_from_ui() {
    NetworkStatus network = backend_.network_status();
    if (!network.ap_active) {
        Result<Unit> result = backend_.start_ap();
        if (!result) {
            show_toast(strings::SYSTEM_WLAN_FAILED, false, "", 3000);
            return;
        }
        show_toast(strings::SYSTEM_TIME_SYNC_HINT, false, "", 4000);
        refresh_system_page();
        return;
    }
    show_toast(strings::SYSTEM_TIME_SYNC_ALREADY, false, "", 4000);
}

void UiAppImpl::ap_stop_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app != nullptr) app->stop_ap_from_ui();
}

void UiAppImpl::ap_extend_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app != nullptr) app->extend_ap_from_ui();
}

void UiAppImpl::time_sync_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app != nullptr) app->sync_time_from_ui();
}

}  // namespace kaffee::ui
