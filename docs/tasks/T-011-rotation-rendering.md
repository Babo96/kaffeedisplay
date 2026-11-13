# T-011 — Fix rotated display rendering (90/180/270)

Status: ready (blocked on hardware/visual iteration)
Track: embedded / display
Owner: –
Branch: –
Issue: https://github.com/Babo96/kaffeedisplay/issues/11

## Problem

Rotation 0° is tear-free after the double-scanout change in PR #10. Rotated
modes (90/180/270) show severe artifacts while scrolling/swiping. The rotated
path still uses the software-rotation buffer/copy path without the VSYNC
framebuffer swap guarantee.

## Acceptance

- Correct rendering and scrolling at 90/180/270, no artifacts.
- Touch mapping correct at all rotations.
- FPS measured at 0/90/180/270; no 0° regression.
- Rotated screenshots in `docs/ui/screenshots/` (simulator/device).
- If not fixed promptly, disable/annotate non-zero rotation in the admin UI and
  fall back safely on the device.

## Investigation notes

- esp_lvgl_port 2.9.0 `avoid_tearing` direct mode expects panel framebuffer
  sources; software rotation produces a separate buffer and is copied into the
  scanned FB.
- Consider a custom VSYNC double-buffer flush that rotates into the back FB, or
  a safe non-tear-free fallback for rotated modes; verify stride/size during
  partial scroll invalidations.
- Test with `ui swipe`, serial `display rotation`, and webcam/60 fps capture.

## Evidence

<!-- link PR/issue, videos, measurements -->
