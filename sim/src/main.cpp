#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "click_test.hpp"
#include "fake_backend.hpp"
#include "kaffee/ui.hpp"
#include "kaffee/ui_control.hpp"
#include "sdl_display.hpp"

namespace {

struct Options {
    std::string screenshot;
    int page = 1;
    int rotation = 0;
    std::string state = "idle";
    int users = 0;
    int book_page = 0;
    std::string order = "usage";
    bool click_test = false;
    bool standby_demo = false;
    bool help = false;
};

uint32_t g_fixed_tick = 0;
bool g_use_fixed_tick = false;

uint32_t tick_cb() {
    if (g_use_fixed_tick) return g_fixed_tick;
    return SDL_GetTicks();
}

void print_usage(const char* argv0) {
    std::printf(
        "Usage: %s [--page 1|2|3] [--rotation 0|90|180|270]\n"
        "          [--screenshot FILE [--state idle|selected|toast|ap|standby]]\n"
        "          [--users N] [--book-page N] [--order creation|usage]\n"
        "          [--click-test] [--standby-demo]\n\n"
        "  --users N   grow the seeded fund to N active members (N >= 5)\n"
        "  --book-page N  show member page N (12 per page) before capturing\n"
        "  --order     member order; `usage` (default) = most bookings this\n"
        "              month first, `creation` = join order\n"
        "  --standby-demo  show animated standby after 3s (simulator only)\n\n"
        "Mouse = touch. Keys: 1/2/3 pages, r rotation, +/- brightness, q quit.\n",
        argv0);
}

bool parse_options(int argc, char** argv, Options* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string* out) {
            if (i + 1 >= argc) return false;
            *out = argv[++i];
            return true;
        };
        if (arg == "--help" || arg == "-h") {
            options->help = true;
        } else if (arg == "--screenshot") {
            if (!next(&options->screenshot)) return false;
        } else if (arg == "--page") {
            std::string value;
            if (!next(&value)) return false;
            options->page = std::atoi(value.c_str());
        } else if (arg == "--rotation") {
            std::string value;
            if (!next(&value)) return false;
            options->rotation = std::atoi(value.c_str());
        } else if (arg == "--state") {
            if (!next(&options->state)) return false;
        } else if (arg == "--users") {
            std::string value;
            if (!next(&value)) return false;
            options->users = std::atoi(value.c_str());
        } else if (arg == "--book-page") {
            std::string value;
            if (!next(&value)) return false;
            options->book_page = std::atoi(value.c_str());
        } else if (arg == "--order") {
            if (!next(&options->order)) return false;
        } else if (arg == "--click-test") {
            options->click_test = true;
        } else if (arg == "--standby-demo") {
            options->standby_demo = true;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            return false;
        }
    }
    if (options->page < 1 || options->page > 3) return false;
    if (options->rotation != 0 && options->rotation != 90 && options->rotation != 180 &&
        options->rotation != 270) {
        return false;
    }
    if (options->state != "idle" && options->state != "selected" && options->state != "toast" &&
        options->state != "ap" && options->state != "standby") {
        return false;
    }
    if (options->users != 0 && options->users < 5) {
        std::fprintf(stderr, "--users must be at least 5 (the seeded fund has 5 members)\n");
        return false;
    }
    if (options->order != "creation" && options->order != "usage") {
        std::fprintf(stderr, "--order must be `creation` or `usage`\n");
        return false;
    }
    if (options->book_page < 0) return false;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    if (options.help) {
        print_usage(argv[0]);
        return 0;
    }

    const bool screenshot_mode = !options.screenshot.empty() || options.click_test;
    if (screenshot_mode) {
        g_use_fixed_tick = true;
        // Keep screenshots independent of the host timezone, locale and display
        // server. The UI renders Europe/Berlin time.
        setenv("TZ", "Europe/Berlin", 1);
        tzset();
        std::setlocale(LC_ALL, "C");
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_RENDER_DRIVER", "software", 1);
    }

    sim::SdlDisplay display;
    auto display_result = display.init();
    if (!display_result) {
        std::fprintf(
            stderr, "Failed to initialise SDL/LVGL: %s\n",
            display_result.error ? display_result.error->message.c_str() : "unknown error");
        return 1;
    }
    if (options.rotation != 0) display.set_rotation(options.rotation);
    lv_tick_set_cb(tick_cb);
    kaffee::ui::attach_display_port(&display);

    sim::FakeBackend backend;
    backend.set_rotation(options.rotation);
    if (options.standby_demo && !screenshot_mode) {
        // In Docker the ESP32 backlight-off circuit is absent. For a clear
        // visible demonstration, show the animated scene after 3 seconds and
        // keep the steam running until the first mouse tap. Real-device
        // dim/off settings and all simulator test cases stay unchanged.
        kaffee::DisplaySettings demo = backend.display_settings();
        demo.dim_timeout_s = 3;
        demo.screen_off_timeout_s = 0;
        backend.apply_display_settings(demo);
    }
    if (options.state == "ap") backend.start_ap();
    if (options.users != 0) backend.set_member_count(options.users);
    if (options.order == "usage") backend.set_usage_order(true);

    auto app_result = kaffee::ui::UiApp::create(backend);
    if (!app_result) {
        std::fprintf(stderr, "Failed to create UI\n");
        return 1;
    }
    std::unique_ptr<kaffee::ui::UiApp> app = std::move(*app_result.value);
    app->start();

    auto* control = dynamic_cast<kaffee::ui::UiControl*>(app.get());
    if (control == nullptr) {
        std::fprintf(stderr, "UI does not expose the simulator control surface\n");
        return 1;
    }

    if (options.click_test) {
        const bool ok = sim::run_click_test(backend, *app, *control, display, g_fixed_tick);
        kaffee::ui::detach_display_port(&display);
        return ok ? 0 : 1;
    }

    if (options.page != 1) control->show_page(options.page - 1, false);

    if (options.state == "selected" || options.state == "toast") {
        const auto& users = backend.users();
        if (!users.empty()) control->debug_select_user(users.front().id);
    }
    if (options.state == "toast") {
        const auto& products = backend.products();
        if (!products.empty()) control->debug_book(products.front().id);
    }

    if (screenshot_mode) {
        for (int frame = 0; frame < 60; ++frame) {
            g_fixed_tick += 16;
            lv_timer_handler();
        }
        if (options.book_page > 0) {
            control->debug_book_page(options.book_page);
            for (int frame = 0; frame < 30; ++frame) {
                g_fixed_tick += 16;
                lv_timer_handler();
            }
        }
        if (options.state == "standby") {
            // Capture the real idle state without waiting one minute in CI.
            // The board would dim the backlight at the same configured time.
            g_fixed_tick += 61'000;
            for (int frame = 0; frame < 18; ++frame) {
                g_fixed_tick += 16;
                lv_timer_handler();
            }
            if (!control->standby_active()) {
                std::fprintf(stderr, "Standby did not activate after dim timeout\n");
                return 1;
            }
        }
        if (!display.save_screenshot(options.screenshot)) {
            std::fprintf(stderr, "Failed to write screenshot: %s\n", options.screenshot.c_str());
            return 1;
        }
        std::printf(
            "screenshot: %s (page %d, rotation %d, state %s, users %d, book_page %d, "
            "order %s)\n",
            options.screenshot.c_str(), options.page, options.rotation, options.state.c_str(),
            options.users, options.book_page, options.order.c_str());
        return 0;
    }

    bool running = true;
    while (running) {
        std::vector<sim::SimKey> keys;
        running = display.poll_events(&keys);
        for (sim::SimKey key : keys) {
            switch (key) {
                case sim::SimKey::Page1:
                    control->show_page(0, true);
                    break;
                case sim::SimKey::Page2:
                    control->show_page(1, true);
                    break;
                case sim::SimKey::Page3:
                    control->show_page(2, true);
                    break;
                case sim::SimKey::Rotate: {
                    int rotation = (display.rotation() + 90) % 360;
                    display.set_rotation(rotation);
                    backend.set_rotation(rotation);
                    app->refresh_data();
                    break;
                }
                case sim::SimKey::BrightnessUp: {
                    int brightness = display.brightness() + 10;
                    if (brightness > 100) brightness = 100;
                    display.set_backlight(brightness);
                    backend.set_brightness(brightness);
                    app->refresh_data();
                    break;
                }
                case sim::SimKey::BrightnessDown: {
                    int brightness = display.brightness() - 10;
                    if (brightness < 5) brightness = 5;
                    display.set_backlight(brightness);
                    backend.set_brightness(brightness);
                    app->refresh_data();
                    break;
                }
            }
        }
        lv_timer_handler();
        SDL_Delay(5);
    }

    return 0;
}
