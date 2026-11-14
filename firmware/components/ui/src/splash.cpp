#include <string>

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
constexpr uint32_t kSplashCoffee = 0x6B4A2F;

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

    // Cup in the middle of the screen: body, coffee surface, handle, saucer and
    // three steam wisps. Plain LVGL primitives, so the splash needs no artwork
    // and cannot be the stale framebuffer content that used to show here.
    const int centre_x = 240;
    lv_obj_t* body = add_shape(root, 132, 108, 22, kSplashCup);
    lv_obj_set_pos(body, centre_x - 66, 190);

    lv_obj_t* coffee = add_shape(body, 108, 18, 9, kSplashCoffee);
    lv_obj_set_pos(coffee, 12, 16);

    lv_obj_t* handle = add_shape(root, 52, 52, LV_RADIUS_CIRCLE, kSplashCup);
    lv_obj_set_pos(handle, centre_x + 52, 214);
    lv_obj_t* handle_hole = add_shape(handle, 28, 28, LV_RADIUS_CIRCLE, kSplashBackground);
    lv_obj_center(handle_hole);

    lv_obj_t* saucer = add_shape(root, 196, 16, 8, kSplashCup);
    lv_obj_set_pos(saucer, centre_x - 98, 302);

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
