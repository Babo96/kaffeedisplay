# ADR 0016: Native LVGL standby coffee animation

Status: Proposed

## Context

The Guition ESP32-S3 kiosk already supports administrative dim and screen-off
timers. We want a recognisable, gentle, coffee-themed standby view without
exhausting the limited 3 MiB OTA application partition and without introducing
a large full-screen GIF or a new image decoder. The separate illustrated-avatar
PR must remain unchanged.

## Decision

- Keep the existing persisted `DisplaySettings` unchanged and let the board
  control physical backlight power. The UI observes LVGL input inactivity and
  shows an overlay only after the configured dim or off threshold.
- Author one professionally shaded porcelain espresso cup in editable SVG.
  Convert it **only during the build** (pinned CairoSVG + Pillow tooling) into
  a fixed 242 × 175 LVGL I8 image with a 256-color BGRA palette and pixel
  indices (43,374 bytes of image data). A static LVGL image uses the built-in
  indexed renderer; the ESP32 does not parse SVGs, use network images or decode
  PNGs.
- Generate the C++ image header under the ignored build directory. Do not commit
  generated firmware image data; the compiled 43,374-byte resource is counted
  against the 3 MiB OTA application partition by the firmware size check.
- Keep the three rising steam wisps as native LVGL lines. A single `80 ms`
  timer fades them independently with 3.2-second phase offsets and ~56 pixels
  rise. Animation needs no frame buffers or runtime geometry allocations.
- Pause the animation after the configured screen-off threshold, without
  removing the full-screen overlay. The next touch is intercepted and wakes
  the screen instead of triggering a blind booking. The overlay disappears
  on activity.
- Disable the standby view whenever the admin AP is open or the rotation/test
  overlay is active; do not change finance, ledger or REST interfaces.
- Provide deterministic simulator screenshot evidence and explicit movement,
  wake and animation-pause checks.

## Consequences and limitations

- The user cannot interact with the booking page through the standby overlay
  until the first wake tap is received. This is intentional.
- Animations are drawn only during the dimmed interval, not when the backlight
  is off. This reduces unnecessary rendering and power consumption, but is not
  deep sleep: CPU and display hardware remain controlled by existing firmware.
- Board and LVGL input activity share the same physical touch source; a real
  ESP32 hardware soak test is required for timing/energy validation.
- No new runtime dependency, file format or settings migration is required.
  Build tooling is pinned in `tools/requirements-standby.txt`; the native
  Cairo library is needed only while building the asset.

## Alternatives

A GIF or frame-by-frame RGB565 animation would consume significantly more
flash and require decoding or large embedded resources. Pure LVGL polygon
contours were smaller, but looked visibly hand-drawn on the 480px screen;
the 43 KB indexed static cup balances image quality and OTA budget.
Only the steam remains animated as native LVGL primitives.
