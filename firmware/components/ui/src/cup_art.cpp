#include "cup_art.hpp"

#include "theme.hpp"

namespace kaffee::ui::cup_art {

namespace {

constexpr uint32_t kCupColor = 0xF2E9DC;
constexpr uint32_t kCoffeeColor = 0x6B4A2F;

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

}  // namespace

void create(lv_obj_t* parent, int center_x, int body_top, uint32_t handle_background) {
    lv_obj_t* body = add_shape(parent, 132, 108, 22, kCupColor);
    lv_obj_set_pos(body, center_x - 66, body_top);

    lv_obj_t* coffee = add_shape(body, 108, 18, 9, kCoffeeColor);
    lv_obj_set_pos(coffee, 12, 16);

    lv_obj_t* handle = add_shape(parent, 52, 52, LV_RADIUS_CIRCLE, kCupColor);
    lv_obj_set_pos(handle, center_x + 52, body_top + 24);
    lv_obj_t* handle_hole = add_shape(handle, 28, 28, LV_RADIUS_CIRCLE, handle_background);
    lv_obj_center(handle_hole);

    lv_obj_t* saucer = add_shape(parent, 196, 16, 8, kCupColor);
    lv_obj_set_pos(saucer, center_x - 98, body_top + 112);
}

}  // namespace kaffee::ui::cup_art
