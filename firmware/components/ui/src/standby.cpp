#include "ui_internal.hpp"

#include "standby_cup_generated.hpp"
#include "strings.hpp"
#include "theme.hpp"

namespace kaffee::ui {

namespace {

// A nine-second cycle with three strokes offset by one third of the period.
// Each stroke gently fades in, rises, fades out, then stays invisible until
// its next turn. One LVGL timer is shared by all three; no bitmap frames.
constexpr uint32_t kSteamFrameMs = 80;
constexpr uint32_t kSteamCycleFrames = 120;
constexpr uint32_t kSteamStaggerFrames = kSteamCycleFrames / 3;
constexpr uint32_t kSteamVisibleFrames = 68;
constexpr int kSteamPeakOpacity = 194;

// Static geometry for the three animated steam wisps. The cup itself is
// rendered from one colour-indexed SVG-derived image in flash.
static lv_point_precise_t kSteamPath[] = {
    {14, 82}, {16, 80}, {16, 77}, {17, 75}, {17, 73}, {17, 71}, {17, 68}, {16, 66},
    {15, 64}, {14, 62}, {13, 59}, {11, 57}, {10, 55}, {10, 52}, {9, 50},  {9, 48},
    {8, 46},  {8, 43},  {9, 41},  {9, 39},  {9, 36},  {10, 34}, {10, 32}, {11, 30},
    {12, 27}, {12, 25}, {13, 23}, {14, 21}, {14, 18}, {15, 16}, {15, 14}, {16, 11},
    {16, 9},  {16, 7},  {16, 5},  {15, 2},  {14, 0},
};
constexpr uint32_t kSteamPathCount = 37;

lv_obj_t* stroke(lv_obj_t* parent, lv_point_precise_t* points, uint32_t count, uint32_t color,
                 int thickness, lv_opa_t opacity = LV_OPA_COVER) {
    lv_obj_t* line = lv_line_create(parent);
    lv_line_set_points(line, points, count);
    lv_obj_set_style_line_width(line, thickness, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, theme::color(color), 0);
    lv_obj_set_style_line_opa(line, opacity, 0);
    lv_obj_set_clickable(line, false);
    return line;
}

}  // namespace

void UiAppImpl::build_standby() {
    standby_overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(standby_overlay_);
    lv_obj_set_size(standby_overlay_, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(standby_overlay_, 0, 0);
    lv_obj_set_style_bg_color(standby_overlay_, theme::color(0x100D0C), 0);
    lv_obj_set_style_bg_opa(standby_overlay_, LV_OPA_COVER, 0);
    lv_obj_set_clickable(standby_overlay_, true);
    lv_obj_set_scrollable(standby_overlay_, false);
    lv_obj_add_event_cb(standby_overlay_, &UiAppImpl::standby_wake_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* scene = lv_obj_create(standby_overlay_);
    lv_obj_remove_style_all(scene);
    lv_obj_set_size(scene, 300, 340);
    lv_obj_center(scene);
    lv_obj_set_clickable(scene, false);
    lv_obj_set_scrollable(scene, false);

    lv_obj_t* title = theme::make_label(scene, strings::STANDBY_TITLE, theme::font_body(),
                                        theme::color(theme::COLOR_ACCENT));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    // Draw the edited SVG as one compact LVGL I8 image. Only its 8-bit palette
    // and pixel indices live in flash (~43 KiB); no SVG renderer at runtime.
    lv_obj_t* cup = lv_image_create(scene);
    lv_image_set_src(cup, &standby_cup_image);
    lv_obj_set_pos(cup, 45, 132);
    lv_obj_set_clickable(cup, false);
    lv_obj_set_scrollable(cup, false);

    for (int i = 0; i < 3; ++i) {
        steam_[i] = stroke(scene, kSteamPath, kSteamPathCount, 0xE9DFD1, 2, LV_OPA_TRANSP);
    }

    lv_obj_t* hint = theme::make_label(scene, strings::STANDBY_TOUCH_HINT, theme::font_small(),
                                       theme::color(theme::COLOR_MUTED));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);

    standby_timer_ = lv_timer_create(&UiAppImpl::standby_anim_cb, kSteamFrameMs, this);
    lv_timer_pause(standby_timer_);
    lv_obj_set_hidden(standby_overlay_, true);
}

void UiAppImpl::hide_standby() {
    if (standby_overlay_ == nullptr) return;
    standby_active_ = false;
    standby_animating_ = false;
    lv_timer_pause(standby_timer_);
    lv_obj_set_hidden(standby_overlay_, true);
}

void UiAppImpl::update_standby() {
    if (standby_overlay_ == nullptr) return;

    // The board remains the sole owner of actual dim/off backlight levels.
    const DisplaySettings settings = backend_.display_settings();
    const uint32_t idle_ms = lv_display_get_inactive_time(lv_display_get_default());
    const bool dimmed = settings.dim_timeout_s > 0 &&
                        idle_ms >= static_cast<uint32_t>(settings.dim_timeout_s) * 1000U;
    const bool off = settings.screen_off_timeout_s > 0 &&
                     idle_ms >= static_cast<uint32_t>(settings.screen_off_timeout_s) * 1000U;

    if (last_ap_active_ || test_overlay_ != nullptr || (!dimmed && !off)) {
        if (standby_active_) hide_standby();
        return;
    }

    if (!standby_active_) {
        standby_active_ = true;
        lv_obj_set_hidden(standby_overlay_, false);
        lv_obj_move_foreground(standby_overlay_);
    }

    if (dimmed && !off) {
        if (!standby_animating_) {
            standby_animating_ = true;
            lv_timer_resume(standby_timer_);
            animate_standby();
        }
    } else if (standby_animating_) {
        // No more framebuffer updates while the physical display is off.
        // The overlay still consumes the first touch (no accidental booking).
        standby_animating_ = false;
        lv_timer_pause(standby_timer_);
    }
}

void UiAppImpl::animate_standby() {
    if (!standby_active_ || !standby_animating_) return;

    const uint32_t tick = lv_tick_get() / kSteamFrameMs;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t phase = (tick + i * kSteamStaggerFrames) % kSteamCycleFrames;
        lv_opa_t opacity = LV_OPA_TRANSP;
        if (phase < 18U) {
            opacity = static_cast<lv_opa_t>(phase * kSteamPeakOpacity / 18U);
        } else if (phase < 30U) {
            opacity = static_cast<lv_opa_t>(kSteamPeakOpacity);
        } else if (phase < kSteamVisibleFrames) {
            opacity = static_cast<lv_opa_t>((kSteamVisibleFrames - phase) * kSteamPeakOpacity /
                                            (kSteamVisibleFrames - 30U));
        }

        // Each strand rises ~56px and drifts side-to-side by only 3px.
        // Gradual opacity makes the start/end blend into the dark background.
        const int rise =
            static_cast<int>((phase < kSteamVisibleFrames ? phase : kSteamVisibleFrames) * 56U /
                             kSteamVisibleFrames);
        const int wobble_phase = static_cast<int>(phase % 32U);
        const int wobble = wobble_phase < 16 ? wobble_phase / 5 - 2 : (32 - wobble_phase) / 5 - 2;
        lv_obj_set_pos(steam_[i], 108 + static_cast<int>(i) * 36 + wobble, 107 - rise);
        lv_obj_set_style_line_opa(steam_[i], opacity, 0);
    }
}

void UiAppImpl::standby_anim_cb(lv_timer_t* timer) {
    auto* app = static_cast<UiAppImpl*>(lv_timer_get_user_data(timer));
    if (app != nullptr) app->animate_standby();
}

void UiAppImpl::standby_wake_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app != nullptr) app->wake();
}

}  // namespace kaffee::ui
