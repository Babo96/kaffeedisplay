# T-011 — Fix rotated display rendering (90/180/270)

Status: investigating (root cause identified, fix path decided)
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

1. **Preferred: hardware rotation.** Set `flags.sw_rotate = false` and let
   `lvgl_port_disp_rotation_update()` drive `esp_lcd_panel_swap_xy()` /
   `esp_lcd_panel_mirror()` on `LV_EVENT_RESOLUTION_CHANGED`. `direct_mode` +
   `avoid_tearing` then stay zero-copy and tear-free at every rotation, and the
   third buffer disappears. Risk: the ST7701S in RGB mode has no frame memory,
   so MADCTL `MV` (transpose) may be ignored — 180° should work (mirror X + Y),
   90/270 must be measured on the panel before committing to this path.
2. **If the panel ignores transpose: LVGL draw-time matrix rotation.**
   `lv_display_set_matrix_rotation()` is LVGL 9.6's sanctioned way to rotate in
   direct/full mode (it rotates at draw time, so the flush stays a no-op).
   `esp_lvgl_port` 2.9.0 does not use it; enabling `LV_DRAW_TRANSFORM_USE_MATRIX`
   and calling it from the board is the smallest in-tree change. Cost: software
   transform per pixel, so measure fps.
3. **If neither works: partial-buffer software rotation.** Drop `direct_mode`
   (and `avoid_tearing`) for rotated modes and use `LV_DISPLAY_RENDER_MODE_PARTIAL`
   with `sw_rotate = true`, which is the combination the port documents. Loses
   tear-freedom and zero-copy, so it needs the fps number before adoption.
4. **Interim guard regardless of the path chosen:** reject or annotate non-zero
   rotation in the admin UI and the serial CLI (`display rotation`) so a user
   cannot put a device into a visibly broken state; keep 0° as the only
   supported value until the fix lands.

## Acceptance

- Correct rendering and scrolling at 90/180/270, no shift, duplication or stale
  bands; touch mapping correct at all rotations.
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

