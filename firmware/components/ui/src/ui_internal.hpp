#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "kaffee/ui.hpp"
#include "kaffee/ui_control.hpp"
#include "lvgl.h"

namespace kaffee::ui {

lv_color_t parse_hex_color(const std::string& value, uint32_t fallback);

// Cross-thread hand-off between IUiBackend::set_change_callback() (service or
// HTTP thread) and the LVGL task. The callback only sets a flag; the LVGL task
// drains it from tick()/housekeeping().
class ChangeQueue {
  public:
    void push();
    bool take();

  private:
    std::mutex mutex_;
    bool pending_ = false;
};

class UiAppImpl final : public UiApp, public UiControl {
  public:
    explicit UiAppImpl(IUiBackend& backend);
    ~UiAppImpl() override;

    void start() override;
    void refresh_data() override;
    void show_test_pattern() override;
    void wake() override;
    void tick() override;

    void show_page(int index, bool animate) override;
    int page() const override;
    bool test_pattern_active() const override;
    bool guest_selected() const override;
    void debug_select_user(const std::string& user_id) override;
    void debug_book(const std::string& product_id) override;
    void debug_deselect() override;

  private:
    struct IdContext {
        UiAppImpl* app;
        std::string id;
    };

    enum StatIndex {
        STAT_TEMPERATURE,
        STAT_UPTIME,
        STAT_HEAP,
        STAT_PSRAM,
        STAT_STORAGE,
        STAT_FIRMWARE,
        STAT_MAC,
        STAT_NETWORK,
        STAT_DRINKS_TODAY,
        STAT_COUNT,
    };

    void build();
    lv_obj_t* build_header(lv_obj_t* parent, const char* title, lv_obj_t** clock_label);
    void build_page_book(lv_obj_t* tile);
    void build_page_stats(lv_obj_t* tile);
    void build_page_system(lv_obj_t* tile);
    void build_page_dots();
    void build_toast();
    void build_system_body();

    void refresh_book_page();
    void refresh_stats_page();
    void refresh_system_page();
    void update_headers();
    void update_system_dynamic();
    void update_dots();
    void set_stay_awake(bool enabled);
    void update_system_indicator();

    void clear_selection();
    void select_user(const User& user);
    void select_guest();
    void book_product(const Product& product);
    void show_toast(const std::string& text, bool undoable, const std::string& event_id,
                    uint32_t duration_ms);
    void hide_toast();
    void undo_last_booking();

    void housekeeping();
    void apply_hold_state();
    void start_ap_from_ui();
    void stop_ap_from_ui();
    void extend_ap_from_ui();
    void sync_time_from_ui();
    void remove_test_pattern();

    std::string next_request_id(const char* prefix);
    uint64_t book_catalog_fingerprint() const;

    static void housekeeping_cb(lv_timer_t* timer);
    static void test_timeout_cb(lv_timer_t* timer);
    static void delete_context_cb(lv_event_t* event);
    static void user_click_cb(lv_event_t* event);
    static void product_click_cb(lv_event_t* event);
    static void change_user_cb(lv_event_t* event);
    static void guest_click_cb(lv_event_t* event);
    static void undo_cb(lv_event_t* event);
    static void month_prev_cb(lv_event_t* event);
    static void month_next_cb(lv_event_t* event);
    static void dot_click_cb(lv_event_t* event);
    static void ap_hold_pressed_cb(lv_event_t* event);
    static void ap_hold_released_cb(lv_event_t* event);
    static void ap_stop_cb(lv_event_t* event);
    static void ap_extend_cb(lv_event_t* event);
    static void time_sync_cb(lv_event_t* event);
    static void test_overlay_click_cb(lv_event_t* event);
    static void tileview_changed_cb(lv_event_t* event);

    IUiBackend& backend_;
    ChangeQueue queue_;
    // UI-originated writes update their visible widgets in place; suppress the
    // service change callback once so they do not trigger a full page rebuild.
    std::atomic<bool> suppress_change_{false};
    uint32_t request_counter_ = 0;
    bool started_ = false;

    lv_obj_t* tileview_ = nullptr;
    lv_obj_t* tiles_[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* clock_labels_[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* dots_bar_ = nullptr;
    lv_obj_t* dot_indicators_[3] = {nullptr, nullptr, nullptr};
    int current_page_ = 0;
    uint32_t page_entered_at_ = 0;
    uint32_t last_header_update_ = 0;
    uint32_t last_system_dynamic_ms_ = 0;

    // Page 1
    lv_obj_t* book_body_ = nullptr;
    lv_obj_t* balance_label_ = nullptr;
    uint64_t last_book_fingerprint_ = 0;
    std::string selected_user_id_;
    bool guest_mode_ = false;
    std::map<std::string, uint32_t> last_book_tick_;
    uint32_t last_book_activity_ = 0;

    // Page 2
    lv_obj_t* stats_body_ = nullptr;
    lv_obj_t* stats_month_label_ = nullptr;
    lv_obj_t* stats_prev_button_ = nullptr;
    lv_obj_t* stats_next_button_ = nullptr;
    lv_obj_t* stats_list_ = nullptr;
    lv_obj_t* stats_warning_ = nullptr;
    lv_obj_t* stats_guest_label_ = nullptr;
    int month_offset_ = 0;
    bool stats_dirty_ = false;

    // Page 3
    lv_obj_t* system_body_ = nullptr;
    lv_obj_t* wlan_status_label_ = nullptr;
    lv_obj_t* wlan_countdown_label_ = nullptr;
    lv_obj_t* system_time_big_ = nullptr;
    lv_obj_t* system_time_date_ = nullptr;
    lv_obj_t* system_time_source_ = nullptr;
    lv_obj_t* system_time_last_ = nullptr;
    lv_obj_t* system_time_quality_ = nullptr;
    lv_obj_t* system_time_warning_ = nullptr;
    lv_obj_t* stat_values_[STAT_COUNT] = {};
    bool last_ap_active_ = false;
    bool stay_awake_ = false;
    bool last_sta_connected_ = false;
    bool last_time_trusted_ = false;
    bool last_must_change_ = false;
    bool system_dirty_ = false;
    DisplaySettings last_display_;

    bool ap_hold_active_ = false;
    uint32_t ap_hold_started_ = 0;
    lv_obj_t* ap_hold_button_ = nullptr;
    lv_obj_t* ap_hold_label_ = nullptr;

    // Toast
    lv_obj_t* toast_ = nullptr;
    lv_obj_t* toast_label_ = nullptr;
    lv_obj_t* toast_undo_ = nullptr;
    std::string toast_event_id_;
    uint32_t toast_shown_at_ = 0;
    uint32_t toast_duration_ms_ = 3000;
    bool toast_undoable_ = false;
    bool toast_visible_ = false;

    // Test pattern
    lv_obj_t* test_overlay_ = nullptr;
    lv_timer_t* test_timer_ = nullptr;

    lv_timer_t* housekeeping_timer_ = nullptr;

    friend struct IdContext;
};

}  // namespace kaffee::ui
