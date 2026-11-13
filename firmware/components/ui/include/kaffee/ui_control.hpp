#pragma once

#include <string>

namespace kaffee::ui {

class UiApp;

// Optional control surface implemented by the LVGL app and used by the host
// simulator and screenshot tooling. It is deliberately separate from the
// frozen ui.hpp contract: firmware only needs UiApp, while tests can switch
// pages and create deterministic UI states without synthesising touch points.
class UiControl {
  public:
    virtual ~UiControl() = default;
    virtual void show_page(int index, bool animate) = 0;
    virtual int page() const = 0;
    virtual bool test_pattern_active() const = 0;
    virtual bool guest_selected() const = 0;
    virtual void debug_select_user(const std::string& user_id) = 0;
    virtual void debug_book(const std::string& product_id) = 0;
    virtual void debug_deselect() = 0;
    // Shows the given page of the booking page's member grid (test/tooling only).
    virtual void debug_book_page(int page) = 0;
};

// Returns the control surface for the concrete UiApp implementation. Firmware
// creates UiAppImpl, so the static cast is valid; returns null for a null app.
UiControl* control_for(UiApp* app);

}  // namespace kaffee::ui
