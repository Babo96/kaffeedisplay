# T-011 — Fix rotated display rendering (90/180/270)

Status: mitigated (rotation fixed at 0); rotated flush is the remaining fix
Track: embedded / display
Owner: –
Branch: –
Issue: https://github.com/Babo96/kaffeedisplay/issues/11

## Problem

Rotation 0° is tear-free and correct after the double-scanout change in PR #10.
Any non-zero rotation breaks the whole display: content stays upright but lands
in the wrong place — shifted rows, duplicated strips, a wrapped header and bands
of the previous frame.

## Root cause (confirmed against the component source)

`esp_lvgl_port` 2.9.0 documents rotation + direct mode as unsupported
(`README.md`: "When using software rotation, you cannot use neither
`direct_mode` nor `full_refresh`"), and this build sets exactly that pair:

| Setting | Where | Consequence |
|---|---|---|
| `flags.sw_rotate = true` | `board.cpp` `init_lvgl()` | All hardware rotation (`rotation.swap_xy/mirror_*`) is skipped: `lvgl_port_disp_rotation_update()` returns early for `sw_rotate`. |
| `flags.direct_mode = true` | same | LVGL renders each *invalidated area* straight into the framebuffer; `flush_cb` gets the area but `color_map` is the framebuffer base. |
| `avoid_tearing = true`, `num_fbs = 2` | `board.cpp` `init_panel()` | LVGL's draw buffers *are* the panel framebuffers, so rendered pixels sit where the scanout reads them. |

In `lvgl_port_flush_callback()` the rotation path (`esp_lvgl_port_disp.c`,
2.9.0) rotates `ww × hh` **from the framebuffer base pointer** into
`draw_buffs[2]` — or, on ESP32-S3, into that buffer with `lv_draw_sw_rotate` —
and then rewrites LVGL's own `refreshed_area`. With direct mode only the full
frame may be rotated, so every partial flush rotates the wrong region. The
pixels are valid; the addressing is not.

Two second-order costs, both active today: `sw_rotate` allocates a third
full-screen buffer, and because that buffer is not a panel framebuffer,
`esp_lcd_rgb_panel_draw_bitmap()` falls back to a full-frame CPU copy plus a
vsync wait — the zero-copy path is lost even at 90/180/270 *if* the rotation
were correct.

Why it never crashes: the panel is 480 × 480, so LVGL's `lv_draw_buf_reshape()`
for the rotated resolution succeeds and `LV_ASSERT_NULL` never fires. On a
non-square panel the same configuration would assert.

## Field evidence

Six phone photos of the panel (rotation changes issued over the serial CLI,
then deleted — see "Evidence handling"):

- Header drawn twice, ~12 % vertical offset, plus a wrapped header fragment at
  the bottom edge.
- The card grid duplicated horizontally (~13–16 % offset, second copy dimmer),
  with the statistics page composited over the right third of the same frame.
- One frame with a band of the *previous* 180° render at the top (upside-down
  bottom row `Zfz/Tgn/Gvn`) over an upright 0° render — proof that the rotation
  really was applied and only part of the panel was repainted.
- One frame at 90/270-style panning: two pages side by side, the right one
  amputated at the panel edge, content wider than 480 px.
- Every frame: colours correct, glyphs crisp, backlight on, **no noise, no
  staircase, no channel swap**. The failure is purely geometric.

That signature — correct pixels, wrong placement, wide canvas, stale bands — is
exactly what "rotate a partial area from the framebuffer base" produces, and it
rules out a panel-timing or PSRAM-underrun cause (which would corrupt pixels).

## Fix path

1. ~~**Hardware rotation** (`sw_rotate = false` +
   `esp_lcd_panel_swap_xy()/mirror()`)~~ **Rejected after reading the IDF
   driver.** On RGB panels `esp_lcd_panel_swap_xy()` only sets a rotate mask that
   `esp_lcd_rgb_panel_draw_bitmap()` applies in its *CPU-copy* branch. With
   `avoid_tearing` the draw buffer is one of the panel framebuffers, so that
   branch never runs and the call is a no-op. The ST7701 vendor driver overrides
   only `mirror` (SDIR/ML) and never writes MADCTL `MV`, so there is no transpose
   path at all.
   `sw_rotate` is nevertheless now off, to silence the broken path and drop the
   third full-screen PSRAM buffer.
2. ~~**LVGL draw-time matrix rotation**~~ **Rejected: not implemented by the
   software renderer.** `lv_display_set_matrix_rotation()` only flips a flag that
   `lv_refr` consumes; the matrix is read exclusively by the VG-Lite and NanoVG
   draw units. With `LV_DRAW_TRANSFORM_USE_MATRIX=1` and RGB565 SW on a
   480 × 480 direct-mode display, LVGL rotates the clip areas and the flush area
   but draws the pixels unrotated — so the image stays put while pointer input is
   rotated. Verified by building LVGL 9.6 from source and rendering a marker at
   rotation 90/180 (marker stayed at 0,0; on a non-square display it also writes
   out of bounds).
3. **If neither works: partial-buffer software rotation.** Drop `direct_mode`
   (and `avoid_tearing`) for rotated modes and use `LV_DISPLAY_RENDER_MODE_PARTIAL`
   with `sw_rotate = true`, which is the combination the port documents. Loses
   tear-freedom and zero-copy, so it needs the fps number before adoption.
4. **Shipped guard (this branch).** Non-zero rotation is rejected by
   `PUT /display` and `PUT /settings` (`validate_display`), refused by the serial
   `display rotation` command, disabled in the admin UI with a German hint, and
   clamped to 0 at boot so a device that stored 90 with older firmware still
   starts usable. Rotation 0 keeps the zero-copy VSYNC path untouched.
5. **Remaining real fix: a rotated flush.** Render strips with LVGL in
   `LV_DISPLAY_RENDER_MODE_PARTIAL`, transpose each strip into the back panel
   framebuffer (stride-aware, RGB565) and swap at VSYNC. ESP32-S3 has no PPA, so
   this is a per-pixel software transpose of ~230k px per full frame — measure
   before promising 30 fps. It also gives up direct-mode zero-copy, which is why
   it is not part of this change. Keep 0° as the default and expose rotated modes
   only behind an explicit opt-in.

## Acceptance

- **Interim (done in T-013's branch):** a device cannot be put into a rotated
  state — API/serial/admin reject it, boot clamps it, rotation 0 is unchanged and
  the third PSRAM buffer is gone.
- Full acceptance, when the rotated flush lands: correct rendering and scrolling
  at 90/180/270, no shift, duplication or stale bands; touch mapping correct at
  all rotations.
- FPS measured at 0/90/180/270; no regression at 0°, and the teardown of the
  third buffer reflected in the internal-RAM figure from `status`.
- Rotated simulator/device screenshots committed; `docs/hardware.md` matches the
  implemented path (it currently documents the broken combination).
- If the fix is not prompt, the interim guard from step 4 ships instead.

## Evidence handling

The six phone photos lived in an untracked `temp/` folder in the repository
root. They were analysed, summarised above, then deleted; `/temp/` is now
git-ignored so field photos cannot be committed by accident. No image from that
folder is in git or in any PR.

## Evidence

