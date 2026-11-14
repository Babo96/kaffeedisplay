#include "ui_internal.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

#include "format.hpp"
#include "kaffee/ui_display.hpp"
#include "strings.hpp"
#include "theme.hpp"

namespace kaffee::ui {

namespace {
DisplayPort* g_display_port = nullptr;
}

void attach_display_port(DisplayPort* port) {
    g_display_port = port;
}

void detach_display_port(DisplayPort* port) {
    if (g_display_port == port) g_display_port = nullptr;
}

DisplayPort* active_display_port() {
    return g_display_port;
}

namespace {
constexpr uint32_t kDebounceMs = 275;
constexpr uint32_t kAutoDeselectMs = 30000;
constexpr uint32_t kAutoReturnMs = 120000;
constexpr uint32_t kToastUndoMs = 8000;
constexpr uint32_t kToastInfoMs = 3000;
constexpr uint32_t kApLongPressMs = 3000;
}  // namespace

lv_color_t parse_hex_color(const std::string& value, uint32_t fallback) {
    if (value.size() == 7 && value[0] == '#') {
        char* end = nullptr;
        unsigned long rgb = std::strtoul(value.c_str() + 1, &end, 16);
        if (end != nullptr && *end == '\0') {
            return lv_color_hex(static_cast<uint32_t>(rgb));
        }
    }
    return lv_color_hex(fallback);
}

void ChangeQueue::push() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = true;
}

bool ChangeQueue::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool pending = pending_;
    pending_ = false;
    return pending;
}

Result<std::unique_ptr<UiApp>> UiApp::create(IUiBackend& backend) {
    std::unique_ptr<UiApp> app(new UiAppImpl(backend));
    return Result<std::unique_ptr<UiApp>>::ok(std::move(app));
}

UiControl* control_for(UiApp* app) {
    return app == nullptr ? nullptr : static_cast<UiAppImpl*>(app);
}

UiAppImpl::UiAppImpl(IUiBackend& backend) : backend_(backend) {}

UiAppImpl::~UiAppImpl() {
    set_stay_awake(false);
    backend_.set_change_callback({});
    if (housekeeping_timer_ != nullptr) {
        lv_timer_delete(housekeeping_timer_);
        housekeeping_timer_ = nullptr;
    }
    remove_test_pattern();
    if (toast_ != nullptr) {
        lv_obj_delete(toast_);
        toast_ = nullptr;
    }
    if (dots_bar_ != nullptr) {
        lv_obj_delete(dots_bar_);
        dots_bar_ = nullptr;
    }
    if (tileview_ != nullptr) {
        lv_obj_delete(tileview_);
        tileview_ = nullptr;
    }
}

void UiAppImpl::start() {
    if (started_) return;
    build();
    backend_.set_change_callback([this]() { queue_.push(); });
    housekeeping_timer_ = lv_timer_create(&UiAppImpl::housekeeping_cb, 100, this);
    started_ = true;
    last_book_activity_ = lv_tick_get();
    show_page(0, false);
    update_headers();
    refresh_system_page();
}

void UiAppImpl::build() {
    // The boot splash owns the screen until the real UI exists.
    hide_splash();
    lv_obj_t* screen = lv_screen_active();
    theme::apply_screen(screen);

    tileview_ = lv_tileview_create(screen);
    lv_obj_set_size(tileview_, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(tileview_, 0, 0);
    lv_obj_set_style_bg_opa(tileview_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tileview_, 0, 0);
    lv_obj_set_style_pad_all(tileview_, 0, 0);
    lv_obj_set_scrollbar_mode(tileview_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tileview_, &UiAppImpl::tileview_changed_cb, LV_EVENT_VALUE_CHANGED, this);

    for (int i = 0; i < 3; ++i) {
        tiles_[i] = lv_tileview_add_tile(tileview_, static_cast<uint8_t>(i), 0, LV_DIR_HOR);
        theme::apply_tile(tiles_[i]);
    }

    build_page_book(tiles_[0]);
    build_page_stats(tiles_[1]);
    build_page_system(tiles_[2]);
    build_page_dots();
    build_toast();
}

lv_obj_t* UiAppImpl::build_header(lv_obj_t* parent, const char* title, lv_obj_t** clock_label) {
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), 56);
    lv_obj_set_pos(header, 0, 0);
    theme::make_transparent(header);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(header, 18, 0);
    lv_obj_set_style_pad_right(header, 18, 0);
    lv_obj_set_style_pad_top(header, 8, 0);
    lv_obj_set_style_pad_bottom(header, 0, 0);

    theme::make_label(header, title, theme::font_title_bold(), theme::color(theme::COLOR_TEXT));
    lv_obj_t* clock =
        theme::make_label(header, "--:--", theme::font_body(), theme::color(theme::COLOR_MUTED));
    if (clock_label != nullptr) *clock_label = clock;
    return header;
}

void UiAppImpl::build_page_dots() {
    lv_obj_t* layer = lv_layer_top();
    lv_obj_set_clickable(layer, false);

    dots_bar_ = lv_obj_create(layer);
    lv_obj_set_size(dots_bar_, 144, 26);
    theme::make_transparent(dots_bar_);
    lv_obj_align(dots_bar_, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_flex_flow(dots_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_bar_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots_bar_, 8, 0);

    for (int i = 0; i < 3; ++i) {
        lv_obj_t* target = lv_obj_create(dots_bar_);
        lv_obj_set_size(target, 30, 26);
        theme::make_transparent(target);
        lv_obj_set_clickable(target, true);
        auto* ctx = new IdContext{this, std::to_string(i)};
        lv_obj_add_event_cb(target, &UiAppImpl::dot_click_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(target, &UiAppImpl::delete_context_cb, LV_EVENT_DELETE, ctx);

        lv_obj_t* dot = theme::make_circle(target, 10, theme::color(theme::COLOR_DIM));
        lv_obj_center(dot);
        dot_indicators_[i] = dot;
    }
    update_dots();
}

void UiAppImpl::build_toast() {
    lv_obj_t* layer = lv_layer_top();
    toast_ = lv_obj_create(layer);
    lv_obj_set_size(toast_, 448, 72);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_set_style_bg_color(toast_, theme::color(theme::COLOR_SURFACE_ALT), 0);
    lv_obj_set_style_bg_opa(toast_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toast_, 1, 0);
    lv_obj_set_style_border_color(toast_, theme::color(theme::COLOR_ACCENT), 0);
    lv_obj_set_style_radius(toast_, 20, 0);
    lv_obj_set_style_pad_all(toast_, 12, 0);
    lv_obj_set_flex_flow(toast_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toast_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(toast_, 10, 0);
    lv_obj_set_scrollable(toast_, false);
    theme::hide_scrollbars(toast_);

    toast_label_ =
        theme::make_label(toast_, "", theme::font_body(), theme::color(theme::COLOR_TEXT));
    lv_obj_set_flex_grow(toast_label_, 1);
    lv_label_set_long_mode(toast_label_, LV_LABEL_LONG_MODE_DOTS);

    toast_undo_ = theme::make_button(toast_, strings::BOOK_UNDO, false);
    lv_obj_set_size(toast_undo_, 126, 44);
    lv_obj_add_event_cb(toast_undo_, &UiAppImpl::undo_cb, LV_EVENT_CLICKED, this);
    lv_obj_set_hidden(toast_undo_, true);

    lv_obj_set_hidden(toast_, true);
    toast_visible_ = false;
}

void UiAppImpl::show_toast(const std::string& text, bool undoable, const std::string& event_id,
                           uint32_t duration_ms) {
    if (toast_ == nullptr) return;
    lv_label_set_text(toast_label_, text.c_str());
    toast_event_id_ = event_id;
    toast_undoable_ = undoable;
    toast_duration_ms_ = duration_ms;
    toast_shown_at_ = lv_tick_get();
    if (undoable) {
        lv_obj_set_hidden(toast_undo_, false);
    } else {
        lv_obj_set_hidden(toast_undo_, true);
    }
    lv_obj_set_hidden(toast_, false);
    lv_obj_move_foreground(toast_);
    toast_visible_ = true;
}

void UiAppImpl::hide_toast() {
    if (toast_ == nullptr) return;
    lv_obj_set_hidden(toast_, true);
    toast_event_id_.clear();
    toast_visible_ = false;
    toast_undoable_ = false;
}

void UiAppImpl::undo_last_booking() {
    if (toast_event_id_.empty()) return;
    Result<Event> result = backend_.undo_booking(toast_event_id_, next_request_id("undo"));
    if (result) {
        hide_toast();
        show_toast(strings::BOOK_UNDONE, false, "", kToastInfoMs);
        if (balance_label_ != nullptr) {
            const Cents saldo = backend_.balance(selected_user_id_);
            lv_label_set_text(balance_label_, format_balance(saldo).c_str());
            lv_obj_set_style_text_color(
                balance_label_,
                theme::color(saldo < 0 ? theme::COLOR_NEGATIVE : theme::COLOR_POSITIVE), 0);
        }
        stats_dirty_ = true;
        system_dirty_ = true;
    } else {
        show_toast(strings::BOOK_UNDO_FAILED, false, "", kToastInfoMs);
    }
}

void UiAppImpl::clear_selection() {
    selected_user_id_.clear();
    guest_mode_ = false;
    last_book_activity_ = lv_tick_get();
    // Never rebuild the page from inside the card/button click callback that
    // selected it: lv_obj_clean() would delete the object currently dispatching
    // the event. Housekeeping drains the queue on the next LVGL tick.
    queue_.push();
}

void UiAppImpl::select_user(const User& user) {
    selected_user_id_ = user.id;
    guest_mode_ = false;
    last_book_activity_ = lv_tick_get();
    queue_.push();
}

void UiAppImpl::select_guest() {
    selected_user_id_.clear();
    guest_mode_ = true;
    last_book_activity_ = lv_tick_get();
    queue_.push();
}

void UiAppImpl::book_product(const Product& product) {
    if (guest_mode_ && !backend_.app_settings().guest_enabled) {
        guest_mode_ = false;
        queue_.push();
        show_toast(strings::BOOK_GUEST_DISABLED, false, "", kToastInfoMs);
        return;
    }

    uint32_t now = lv_tick_get();
    auto it = last_book_tick_.find(product.id);
    if (it != last_book_tick_.end() && now - it->second < kDebounceMs) return;
    last_book_tick_[product.id] = now;

    if (!guest_mode_ && selected_user_id_.empty()) return;

    Result<Event> result =
        guest_mode_ ? backend_.guest_sale(product.id, next_request_id("guest"))
                    : backend_.purchase(selected_user_id_, product.id, next_request_id("book"));
    if (!result) {
        show_toast(strings::BOOK_FAILED, false, "", kToastInfoMs);
        return;
    }

    const Event& event = *result.value;
    std::string text;
    if (guest_mode_) {
        text = std::string(strings::BOOK_GUEST) + strings::COMMON_SEPARATOR + event.product_name +
               strings::COMMON_SEPARATOR + format_money(event.unit_amount_cents);
        show_toast(text, false, "", kToastInfoMs);
    } else {
        const Cents saldo = backend_.balance(selected_user_id_);
        text = event.product_name + strings::BOOK_TOAST_SEPARATOR +
               format_money(event.unit_amount_cents) + strings::BOOK_TOAST_SEPARATOR +
               strings::BOOK_BALANCE + " " + format_money(saldo);
        // Update the visible balance immediately; the queued refresh handles
        // stats/leaderboard off the booking event callback.
        if (balance_label_ != nullptr) {
            lv_label_set_text(balance_label_, format_balance(saldo).c_str());
            lv_obj_set_style_text_color(
                balance_label_,
                theme::color(saldo < 0 ? theme::COLOR_NEGATIVE : theme::COLOR_POSITIVE), 0);
        }
        show_toast(text, true, event.id, kToastUndoMs);
    }
    last_book_activity_ = now;
    // The balance/toast were updated in place above. Do not rebuild page 1 from
    // the booking event callback; hidden pages refresh when they are entered.
    stats_dirty_ = true;
    system_dirty_ = true;
}

uint64_t UiAppImpl::book_catalog_fingerprint() const {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    auto mix_text = [&mix](const std::string& text) {
        for (unsigned char c : text) mix(c);
        mix(0xff);
    };
    // The visible member page is part of the rendered state.
    mix(static_cast<uint64_t>(book_page_));
    mix(selected_user_id_.empty() ? 0 : 1);
    mix_text(selected_user_id_);
    mix(guest_mode_ ? 1 : 0);
    mix(backend_.app_settings().guest_enabled ? 1 : 0);
    // Iterate the members in *display* order: when the grid is sorted by usage a
    // re-order has to rebuild page 1, and an unchanged order must not.
    for (const User& user : ordered_members().users) {
        mix_text(user.id);
        mix_text(user.name);
        mix_text(user.color);
        // The avatar is part of the visible card: an admin fixing a wrong motif
        // must reach the panel without waiting for another user/product change.
        mix(static_cast<uint64_t>(user.avatar_id));
    }
    for (const Product& product : backend_.products()) {
        if (!product.active) continue;
        mix_text(product.id);
        mix_text(product.name);
        mix_text(product.category);
        mix_text(product.icon);
        mix_text(product.color);
        mix(static_cast<uint64_t>(product.price_cents));
        mix(static_cast<uint64_t>(product.sort));
    }
    return hash;
}

void UiAppImpl::refresh_data() {
    if (!started_) return;
    if (!selected_user_id_.empty()) {
        auto user = backend_.find_user(selected_user_id_);
        if (!user || !user->active) {
            selected_user_id_.clear();
            guest_mode_ = false;
        }
    }
    const uint64_t fingerprint = book_catalog_fingerprint();
    if (fingerprint != last_book_fingerprint_) {
        refresh_book_page();
    } else if (balance_label_ != nullptr && !selected_user_id_.empty()) {
        // Topups/voids from the API/serial CLI change only the balance; update
        // the visible label instead of rebuilding the whole page.
        const Cents saldo = backend_.balance(selected_user_id_);
        lv_label_set_text(balance_label_, format_balance(saldo).c_str());
        lv_obj_set_style_text_color(
            balance_label_, theme::color(saldo < 0 ? theme::COLOR_NEGATIVE : theme::COLOR_POSITIVE),
            0);
    }
    if (current_page_ == 1) {
        refresh_stats_page();
        stats_dirty_ = false;
    } else {
        stats_dirty_ = true;
    }
    if (current_page_ == 2) {
        refresh_system_page();
        system_dirty_ = false;
    } else {
        system_dirty_ = true;
    }
    update_headers();
}

void UiAppImpl::update_headers() {
    std::string clock = format_clock(backend_.time_info().now);
    for (lv_obj_t* label : clock_labels_) {
        if (label != nullptr) lv_label_set_text(label, clock.c_str());
    }
    // The admin hotspot must hold the panel awake on every page: it can be
    // opened from page 3, the API or the serial console while page 0 is
    // visible. Housekeeping calls this once per second on all pages, so the
    // lock cannot go stale while page 3 is not being drawn.
    set_stay_awake(backend_.network_status().ap_active);
}

void UiAppImpl::set_stay_awake(bool enabled) {
    if (stay_awake_ == enabled) return;
    stay_awake_ = enabled;
    DisplayPort* port = active_display_port();
    if (port != nullptr) port->set_stay_awake(enabled);
}

void UiAppImpl::update_dots() {
    for (int i = 0; i < 3; ++i) {
        if (dot_indicators_[i] == nullptr) continue;
        lv_obj_set_style_bg_color(
            dot_indicators_[i],
            theme::color(i == current_page_ ? theme::COLOR_ACCENT : theme::COLOR_DIM), 0);
    }
}

void UiAppImpl::show_page(int index, bool animate) {
    if (tileview_ == nullptr || index < 0 || index > 2) return;
    lv_tileview_set_tile_by_index(tileview_, static_cast<uint32_t>(index), 0,
                                  animate ? LV_ANIM_ON : LV_ANIM_OFF);
    current_page_ = index;
    page_entered_at_ = lv_tick_get();
    if (index == 2) last_system_dynamic_ms_ = 0;
    if (index == 1 && stats_dirty_) {
        refresh_stats_page();
        stats_dirty_ = false;
    }
    if (index == 2 && system_dirty_) {
        refresh_system_page();
        system_dirty_ = false;
    }
    update_dots();
}

int UiAppImpl::page() const {
    return current_page_;
}

bool UiAppImpl::test_pattern_active() const {
    return test_overlay_ != nullptr;
}

bool UiAppImpl::guest_selected() const {
    return guest_mode_;
}

void UiAppImpl::debug_select_user(const std::string& user_id) {
    auto user = backend_.find_user(user_id);
    if (user) select_user(*user);
}

void UiAppImpl::debug_book(const std::string& product_id) {
    auto product = backend_.find_product(product_id);
    if (product) book_product(*product);
}

void UiAppImpl::debug_deselect() {
    clear_selection();
}

void UiAppImpl::debug_book_page(int page) {
    if (book_body_ == nullptr) return;
    book_page_ = page < 0 ? 0 : page;
    refresh_book_page();
}

void UiAppImpl::tick() {
    if (started_) housekeeping();
}

void UiAppImpl::housekeeping() {
    if (!started_) return;
    if (queue_.take()) refresh_data();

    uint32_t now = lv_tick_get();

    if (toast_visible_ && now - toast_shown_at_ >= toast_duration_ms_) hide_toast();

    if ((!selected_user_id_.empty() || guest_mode_) &&
        now - last_book_activity_ >= kAutoDeselectMs) {
        clear_selection();
    }

    if (stay_awake_) {
        // Keep the panel awake while the admin hotspot is open; the lock is
        // refreshed by update_headers() on every page.
        lv_display_trigger_activity(nullptr);
    }
    if (current_page_ != 0 && now - page_entered_at_ >= kAutoReturnMs) {
        show_page(0, true);
    }

    if (ap_hold_active_ && now - ap_hold_started_ >= kApLongPressMs) {
        ap_hold_active_ = false;
        if (ap_hold_label_ != nullptr) {
            lv_label_set_text(ap_hold_label_, strings::SYSTEM_WLAN_START);
        }
        start_ap_from_ui();
    }

    if (now - last_header_update_ >= 1000 || last_header_update_ == 0) {
        last_header_update_ = now;
        update_headers();
        if (current_page_ == 2) {
            refresh_system_page();
        } else {
            system_dirty_ = true;
        }
    }
}

void UiAppImpl::apply_hold_state() {
    if (ap_hold_button_ == nullptr || ap_hold_label_ == nullptr) return;
    if (ap_hold_active_) {
        lv_label_set_text(ap_hold_label_, strings::SYSTEM_WLAN_HOLD);
    } else {
        lv_label_set_text(ap_hold_label_, strings::SYSTEM_WLAN_START);
    }
}

std::string UiAppImpl::next_request_id(const char* prefix) {
    ++request_counter_;
    const TimeInfo time = backend_.time_info();
    // No flash-backed stats() here: wall clock + LVGL tick + counter are enough
    // for a boot-local id and keep the booking path free of storage I/O.
    return std::string("ui-") + prefix + "-" + std::to_string(time.now) + "-" +
           std::to_string(request_counter_) + "-" + std::to_string(lv_tick_get());
}

void UiAppImpl::wake() {
    lv_display_trigger_activity(nullptr);
}

void UiAppImpl::remove_test_pattern() {
    if (test_timer_ != nullptr) {
        lv_timer_delete(test_timer_);
        test_timer_ = nullptr;
    }
    if (test_overlay_ != nullptr) {
        lv_obj_delete(test_overlay_);
        test_overlay_ = nullptr;
    }
}

void UiAppImpl::show_test_pattern() {
    remove_test_pattern();
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    test_overlay_ = overlay;
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, theme::color(theme::COLOR_BG), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_clickable(overlay, true);
    lv_obj_add_event_cb(overlay, &UiAppImpl::test_overlay_click_cb, LV_EVENT_CLICKED, this);

    const uint32_t colors[4] = {theme::COLOR_ACCENT, theme::COLOR_POSITIVE, theme::COLOR_NEGATIVE,
                                theme::COLOR_SILVER};
    const char* labels[4] = {"0°", "90°", "180°", "270°"};
    const int xs[4] = {0, 240, 0, 240};
    const int ys[4] = {0, 0, 240, 240};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* block = lv_obj_create(overlay);
        lv_obj_set_size(block, 240, 240);
        lv_obj_set_pos(block, xs[i], ys[i]);
        lv_obj_set_style_bg_color(block, theme::color(colors[i]), 0);
        lv_obj_set_style_bg_opa(block, LV_OPA_30, 0);
        lv_obj_set_style_border_width(block, 1, 0);
        lv_obj_set_style_border_color(block, theme::color(colors[i]), 0);
        lv_obj_set_style_radius(block, 0, 0);
        lv_obj_set_clickable(block, false);
        lv_obj_t* text =
            theme::make_label(block, labels[i], theme::font_huge_bold(), theme::color(colors[i]));
        lv_obj_center(text);
    }

    lv_obj_t* card = theme::make_card(overlay);
    lv_obj_set_size(card, 320, 150);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 6, 0);
    theme::make_label(card, strings::TEST_TITLE, theme::font_title_bold(),
                      theme::color(theme::COLOR_TEXT));
    std::string rotation = std::string(strings::TEST_ROTATION) + " " +
                           rotation_label(backend_.display_settings().rotation);
    theme::make_label(card, rotation.c_str(), theme::font_body_bold(),
                      theme::color(theme::COLOR_ACCENT));
    theme::make_label(card, strings::TEST_HINT, theme::font_small(),
                      theme::color(theme::COLOR_MUTED));

    test_timer_ = lv_timer_create(&UiAppImpl::test_timeout_cb, 15000, this);
}

void UiAppImpl::housekeeping_cb(lv_timer_t* timer) {
    auto* app = static_cast<UiAppImpl*>(lv_timer_get_user_data(timer));
    if (app != nullptr) app->housekeeping();
}

void UiAppImpl::test_timeout_cb(lv_timer_t* timer) {
    auto* app = static_cast<UiAppImpl*>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        app->test_timer_ = nullptr;
        if (app->test_overlay_ != nullptr) {
            lv_obj_delete(app->test_overlay_);
            app->test_overlay_ = nullptr;
        }
    }
    lv_timer_delete(timer);
}

void UiAppImpl::test_overlay_click_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app != nullptr) app->remove_test_pattern();
}

void UiAppImpl::delete_context_cb(lv_event_t* event) {
    delete static_cast<IdContext*>(lv_event_get_user_data(event));
}

void UiAppImpl::tileview_changed_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app == nullptr || app->tileview_ == nullptr) return;
    lv_obj_t* active = lv_tileview_get_tile_active(app->tileview_);
    for (int i = 0; i < 3; ++i) {
        if (app->tiles_[i] == active) {
            if (app->current_page_ != i) {
                app->current_page_ = i;
                app->page_entered_at_ = lv_tick_get();
                if (i == 1 && app->stats_dirty_) {
                    app->refresh_stats_page();
                    app->stats_dirty_ = false;
                }
                if (i == 2) {
                    app->last_system_dynamic_ms_ = 0;
                    if (app->system_dirty_) {
                        app->refresh_system_page();
                        app->system_dirty_ = false;
                    }
                }
                app->update_dots();
            }
            break;
        }
    }
}

void UiAppImpl::dot_click_cb(lv_event_t* event) {
    auto* ctx = static_cast<IdContext*>(lv_event_get_user_data(event));
    if (ctx == nullptr || ctx->app == nullptr) return;
    ctx->app->show_page(std::atoi(ctx->id.c_str()), true);
}

void UiAppImpl::undo_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app != nullptr) app->undo_last_booking();
}

}  // namespace kaffee::ui
