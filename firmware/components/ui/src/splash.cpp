#include "cup_art.hpp"
#include "kaffee/ui_control.hpp"
#include "lvgl.h"
#include "strings.hpp"
#include "theme.hpp"

namespace kaffee::ui {

namespace {

// Warm brown, deliberately lighter than COLOR_BG so the splash reads as a
// deliberate screen rather than a blank panel.
constexpr uint32_t kSplashBackground = 0x2A1B12;
constexpr uint32_t kSplashCup = 0xF2E9DC;

lv_obj_t* g_splash = nullptr;

lv_obj_t* add_shape(lv_obj_t* parent, int width, int height, int radius, uint32_t color) {
    lv_obj_t* shape = lv_obj_create(parent);
    lv_obj_set_size(shape, width, height);
    lv_obj_set_style_bg_color(shape, theme::color(color), 0);
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(shape, 0, 0);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_set_style_pad_all(shape, 0, 0);
    lv_obj_set_scrollable(shape, false);
    lv_obj_set_clickable(shape, false);
    return shape;
}

// A short rounded line, used for the steam.
lv_obj_t* add_steam(lv_obj_t* parent, int x, int y, int height) {
    lv_obj_t* line = add_shape(parent, 6, height, 3, kSplashCup);
    lv_obj_set_style_bg_opa(line, LV_OPA_70, 0);
    lv_obj_set_pos(line, x, y);
    return line;
}

}  // namespace

void show_splash() {
    if (g_splash != nullptr) return;
    lv_obj_t* root = add_shape(lv_screen_active(), lv_pct(100), lv_pct(100), 0, kSplashBackground);
    g_splash = root;

    const int centre_x = 240;
    cup_art::create(root, centre_x, 190, kSplashBackground);

    add_steam(root, centre_x - 34, 140, 34);
    add_steam(root, centre_x - 3, 128, 46);
    add_steam(root, centre_x + 28, 140, 34);

    lv_obj_t* label = theme::make_label(root, strings::APP_TITLE, theme::font_title_bold(),
                                        theme::color(kSplashCup));
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -64);

    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(nullptr);
}

void hide_splash() {
    if (g_splash == nullptr) return;
    lv_obj_delete(g_splash);
    g_splash = nullptr;
}

}  // namespace kaffee::ui
