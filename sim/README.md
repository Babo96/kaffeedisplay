# LVGL simulator

The simulator builds the real `ui/` and `domain/` code with LVGL 9.6 and SDL2
plus a fake `IUiBackend`. It is the fastest way to iterate on the device screens
and the source of the README screenshots.

## Requirements

- Linux with SDL2 headers (`libsdl2-dev`), zlib and CMake ≥ 3.20.
- Network access on the first configure: CMake fetches the pinned LVGL tag
  `v9.6.0` (the same version as `firmware/main/idf_component.yml`).

## Build and run

```sh
make sim
# or
cmake -S sim -B sim/build -DCMAKE_BUILD_TYPE=Release
cmake --build sim/build -j
./sim/build/kaffeekasse-sim
```

Mouse click = touch. Keys:

| Key | Action |
|---|---|
| `1` / `2` / `3` | Buchen / Statistik / System |
| `r` | cycle rotation 0 → 90 → 180 → 270 |
| `+` / `-` | brightness up/down |
| `q` / `Esc` | quit |

The simulator draws the display itself (no `esp_lvgl_port`) and applies rotation
in its flush callback, mirroring what the board display port will do.

## Screenshot mode

```sh
./sim/build/kaffeekasse-sim --screenshot out.png --page 1
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --state selected
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --state toast
./sim/build/kaffeekasse-sim --screenshot out.png --page 3 --state ap
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --rotation 90
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --users 30 --scroll 500
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --users 30 --order usage
```

`--users N` grows the seeded fund to `N` active members (minimum 5, the seeded
set) with deterministic names, avatars and booking counts. `--scroll PX` scrolls
the page-1 member list down by `PX` before the capture, and `--order usage`
returns the members with the most bookings this month first (the order T-013
adds to the service). Together they render the "30+ members" states without
inventing a second screenshot pipeline; all three are no-ops by default, so the
five original captures stay byte-identical.

`--state` creates a deterministic UI state through the same code paths as touch
input:

- `idle` — page 1 with user tiles, no selection
- `selected` — first seeded member selected
- `toast` — first member books the first product; 8 s undo toast visible
- `ap` — WLAN hotspot active with Wi-Fi join and admin URL QR codes

Screenshot mode forces the SDL dummy video driver, `TZ=Europe/Berlin`, the C
locale, a fixed wall clock (2026-11-11 09:41 Europe/Berlin) and a fixed 16 ms
tick, so reruns produce the same pixels on hosts with any `TZ`. CI regenerates
the set into a temporary directory and compares decompressed pixels with
`tools/check_screenshots.py` (no ImageMagick/Pillow dependency).

## Regenerate / verify the committed set

```sh
make screenshots        # writes docs/ui/screenshots/01-idle.png … 05-system-ap-qr.png
make screenshots-check  # regenerates to /tmp and compares pixels with the committed set
```

`make screenshots` is the deterministic batch. `make sim` stays the interactive
SDL window; both build the same `kaffeekasse-sim` binary. Commit the screenshots
whenever the UI changes visibly.

## Click/hit-test smoke test

```sh
make test-ui
./sim/build/kaffeekasse-sim --click-test
```

The test drives the real LVGL pointer indev at fixed 480×480 coordinates: avatars,
name labels, product text columns, product badges, guest tile, page-dot centres,
test-pattern dismissal, debounce, undo/undo expiry, guest-no-undo, auto-deselect
and the guest-disabled guard. It does not call the `debug_*` state helpers.

## Fake backend

`src/fake_backend.cpp` seeds five members (four with preset avatars and one
initials-only fallback), six drinks, top-ups, purchases and
guest sales through the real `kaffee::Ledger`. Balances, leaderboards, voids and
undo therefore use production domain code. AP/time/device stats are simulated.
`firmware/components/ui/include/kaffee/ui_display.hpp` is the display port the
board component can implement later; `ui_control.hpp` is the simulator-only page
and state control surface.

## Coffee standby preview

The coffee standby feature uses the same administration-controlled idle
brightness and timeout settings as the ESP32. In the interactive simulator,
wait approximately 60 seconds without clicking the pointer; three
steam strokes will rise and fade in/out at different times. Click once to
dismiss the overlay. Use `--standby-demo` for a **3 second** delay and no
simulated screen-off timeout, as in the default Docker preview.

For a deterministic screenshot without the wait:

```sh
./sim/build/kaffeekasse-sim --screenshot /tmp/standby.png --state standby
```

The `make test-ui` suite checks that the steam actually moves, pauses after
the screen-off deadline, and that the first wake tap never purchases a drink.
You can also run the simulator in a browser through Docker using the
[preview guide](../docs/container-preview.md).
