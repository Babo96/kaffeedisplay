#pragma once

#include <cstdint>

#include "lvgl.h"

namespace kaffee::ui::cup_art {

// Add the shared, non-interactive cup body, coffee surface, handle and saucer.
// center_x and body_top are relative to parent; handle_background fills the
// handle cutout to match the screen behind the cup.
void create(lv_obj_t* parent, int center_x, int body_top, uint32_t handle_background);

}  // namespace kaffee::ui::cup_art
