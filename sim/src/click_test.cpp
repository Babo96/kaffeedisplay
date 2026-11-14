#include "click_test.hpp"

#include <cstdio>

namespace sim {
namespace {

constexpr int kAnnaAvatarX = 84;
constexpr int kAnnaAvatarY = 132;
constexpr int kAnnaNameX = 84;
constexpr int kAnnaNameY = 160;
// The guest action is pinned in the header band (96x40 at 256,8), not in the
// member grid, so it stays reachable once the fund scrolls.
constexpr int kGuestActionX = 304;
constexpr int kGuestActionY = 28;
constexpr int kProductBadgeX = 44;
constexpr int kProductBadgeY = 210;
constexpr int kProductTextX = 150;
constexpr int kProductTextY = 212;
constexpr int kChangeUserX = 400;
constexpr int kChangeUserY = 100;
constexpr int kUndoX = 390;
constexpr int kUndoY = 400;
constexpr int kDot1X = 202;
constexpr int kDot2X = 240;
constexpr int kDot3X = 278;
constexpr int kDotY = 462;
constexpr int kCenterX = 240;
constexpr int kCenterY = 240;

struct Harness {
    FakeBackend& backend;
    kaffee::ui::UiApp& app;
    kaffee::ui::UiControl& control;
    SdlDisplay& display;
    uint32_t& tick;
};

void advance(Harness& h, int frames) {
    for (int i = 0; i < frames; ++i) {
        h.tick += 16;
        lv_timer_handler();
    }
}

void click(Harness& h, int x, int y, int hold_frames = 2, int settle_frames = 4) {
    h.display.inject_pointer(x, y, true);
    advance(h, hold_frames);
    h.display.inject_pointer(x, y, false);
    advance(h, settle_frames);
}

bool expect(bool condition, const char* message) {
    if (!condition) std::printf("click-test FAIL: %s\n", message);
    return condition;
}

}  // namespace

bool run_click_test(FakeBackend& backend, kaffee::ui::UiApp& app, kaffee::ui::UiControl& control,
                    SdlDisplay& display, uint32_t& tick) {
    Harness h{backend, app, control, display, tick};
    bool ok = true;

    ok = expect(control.page() == 0, "starts on page 1") && ok;
    ok = expect(!control.test_pattern_active(), "test pattern starts hidden") && ok;

    const int purchase_base = backend.count_events(kaffee::EventType::Purchase);
    const int guest_base = backend.count_events(kaffee::EventType::GuestSale);
    const int void_base = backend.count_events(kaffee::EventType::Void);

    // Avatar + product badge must reach the tile, not be swallowed by the
    // decorative circle. Then undo must void the member booking.
    click(h, kAnnaAvatarX, kAnnaAvatarY);
    advance(h, 8);
    click(h, kProductBadgeX, kProductBadgeY);
    advance(h, 8);
    ok = expect(backend.count_events(kaffee::EventType::Purchase) == purchase_base + 1,
                "avatar + badge click books immediately") &&
         ok;
    click(h, kUndoX, kUndoY);
    advance(h, 8);
    ok = expect(backend.count_events(kaffee::EventType::Void) == void_base + 1,
                "undo button voids within eight seconds") &&
         ok;
    click(h, kChangeUserX, kChangeUserY);
    advance(h, 8);

    // Name label + product text column must also reach the tile.
    click(h, kAnnaNameX, kAnnaNameY);
    advance(h, 8);
    click(h, kProductTextX, kProductTextY);
    advance(h, 8);
    ok = expect(backend.count_events(kaffee::EventType::Purchase) == purchase_base + 2,
                "name + product text column click books immediately") &&
         ok;
    click(h, kChangeUserX, kChangeUserY);
    advance(h, 8);

    // Two taps inside the 275 ms debounce window must book once.
    click(h, kAnnaAvatarX, kAnnaAvatarY);
    advance(h, 8);
    click(h, kProductBadgeX, kProductBadgeY, 1, 1);
    click(h, kProductBadgeX, kProductBadgeY, 1, 1);
    advance(h, 16);
    ok = expect(backend.count_events(kaffee::EventType::Purchase) == purchase_base + 3,
                "rapid duplicate tap is debounced") &&
         ok;

    // The undo action expires after eight seconds; a click where the button
    // used to be must not create a second void.
    advance(h, 8200 / 16);
    click(h, kUndoX, kUndoY);
    advance(h, 8);
    ok = expect(backend.count_events(kaffee::EventType::Void) == void_base + 1,
                "undo button expires after eight seconds") &&
         ok;
    click(h, kChangeUserX, kChangeUserY);
    advance(h, 8);

    const int purchase_before_autodeselect = backend.count_events(kaffee::EventType::Purchase);

    // Guest avatar + product text column create a cash sale, and the guest
    // toast deliberately has no undo action.
    click(h, kGuestActionX, kGuestActionY);
    advance(h, 8);
    ok = expect(control.guest_selected(), "guest avatar selects guest mode") && ok;
    click(h, kProductTextX, kProductTextY);
    advance(h, 8);
    ok = expect(backend.count_events(kaffee::EventType::GuestSale) == guest_base + 1,
                "guest avatar + product text sells for cash") &&
         ok;
    click(h, kUndoX, kUndoY);
    advance(h, 8);
    ok = expect(backend.count_events(kaffee::EventType::Void) == void_base + 1,
                "guest sale has no undo") &&
         ok;
    click(h, kChangeUserX, kChangeUserY);
    advance(h, 8);

    // Auto-deselect after 30 s: a product-area tap is just a user tile again.
    click(h, kAnnaAvatarX, kAnnaAvatarY);
    advance(h, 8);
    advance(h, 31'000 / 16);
    click(h, kProductTextX, kProductTextY);
    advance(h, 8);
    ok = expect(backend.count_events(kaffee::EventType::Purchase) == purchase_before_autodeselect,
                "auto-deselect after 30 s prevents stray booking") &&
         ok;
    click(h, kChangeUserX, kChangeUserY);
    advance(h, 8);

    // Disabling guest sales clears an existing guest selection and blocks a
    // guest booking until the setting is enabled again.
    click(h, kGuestActionX, kGuestActionY);
    advance(h, 8);
    backend.set_guest_enabled(false);
    app.refresh_data();
    advance(h, 8);
    ok = expect(!control.guest_selected(), "guest selection is cleared when disabled") && ok;
    click(h, kProductTextX, kProductTextY);
    advance(h, 8);
    ok = expect(backend.count_events(kaffee::EventType::GuestSale) == guest_base + 1,
                "guest booking is blocked when disabled") &&
         ok;
    backend.set_guest_enabled(true);
    app.refresh_data();
    advance(h, 8);

    // Page dots are real targets; their decorative circle children must not
    // swallow the click.
    click(h, kDot2X, kDotY);
    advance(h, 8);
    ok = expect(control.page() == 1, "second page dot opens statistics") && ok;
    click(h, kDot3X, kDotY);
    advance(h, 8);
    ok = expect(control.page() == 2, "third page dot opens system") && ok;
    click(h, kDot1X, kDotY);
    advance(h, 8);
    ok = expect(control.page() == 0, "first page dot returns to booking") && ok;

    // The centre card inside the test pattern is decorative; the full-screen
    // overlay must receive the dismiss click.
    app.show_test_pattern();
    advance(h, 4);
    ok = expect(control.test_pattern_active(), "test pattern opens") && ok;
    click(h, kCenterX, kCenterY);
    advance(h, 8);
    ok = expect(!control.test_pattern_active(), "clicking the centre dismisses test pattern") && ok;

    // AP stay-awake goes through the display port, not only LVGL activity.
    backend.start_ap();
    app.refresh_data();
    advance(h, 8);
    ok = expect(display.stay_awake(), "active AP holds the panel awake") && ok;
    backend.stop_ap();
    app.refresh_data();
    advance(h, 8);
    ok = expect(!display.stay_awake(), "stopping AP releases the panel") && ok;

    // Reuse the existing admin dim/off settings; no network or GIF decoder.
    // Jump the LVGL clock instead of making CI wait a real minute.
    ok = expect(!control.standby_active(), "standby starts inactive") && ok;
    h.tick += 61'000;
    advance(h, 8);
    ok = expect(control.standby_active(), "coffee standby appears after dim timeout") && ok;
    const int steam_before = control.standby_steam_y();
    advance(h, 12);
    ok = expect(control.standby_steam_y() != steam_before, "steam actually moves") && ok;

    // Sample a complete 9.6s animation cycle: all three strokes must become
    // invisible, become clearly visible, and take turns at different phases.
    // This catches static steam or a timer that only changes line positions.
    int min_opa[3] = {255, 255, 255};
    int max_opa[3] = {0, 0, 0};
    bool staggered = false;
    for (int frame = 0; frame < 640; ++frame) {
        advance(h, 1);
        int opas[3] = {};
        for (int i = 0; i < 3; ++i) {
            opas[i] = control.standby_steam_opacity(i);
            if (opas[i] < min_opa[i]) min_opa[i] = opas[i];
            if (opas[i] > max_opa[i]) max_opa[i] = opas[i];
        }
        if (opas[0] != opas[1] && opas[1] != opas[2]) staggered = true;
    }
    for (int i = 0; i < 3; ++i) {
        ok = expect(min_opa[i] == 0 && max_opa[i] >= 180,
                    "each steam strand alternately fades out and becomes visible") &&
             ok;
    }
    ok = expect(staggered, "three steam strands fade at staggered times") && ok;

    // After screen-off timeout, the scene remains to consume the first wake
    // tap, but the animation must be paused to save redraw power.
    h.tick += 600'000;
    advance(h, 8);
    ok = expect(control.standby_active(), "off-screen wake target stays present") && ok;
    const int still_steam = control.standby_steam_y();
    advance(h, 24);
    ok = expect(control.standby_steam_y() == still_steam,
                "steam animation pauses while the backlight is off") &&
         ok;

    const int purchases_before_wake = backend.count_events(kaffee::EventType::Purchase);
    click(h, kCenterX, kCenterY);
    advance(h, 8);
    ok = expect(!control.standby_active(), "first touch dismisses standby") && ok;
    ok = expect(backend.count_events(kaffee::EventType::Purchase) == purchases_before_wake,
                "first wake touch never purchases a drink") &&
         ok;

    if (ok) {
        std::printf("click-test: PASS (purchases %d, guest sales %d, voids %d)\n",
                    backend.count_events(kaffee::EventType::Purchase),
                    backend.count_events(kaffee::EventType::GuestSale),
                    backend.count_events(kaffee::EventType::Void));
    }
    return ok;
}

}  // namespace sim
