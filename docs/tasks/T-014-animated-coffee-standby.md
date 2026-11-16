# T-014 — Animated coffee idle / power-save display

Status: in-progress
Track: embedded UI
Owner: 42vibes
Branch: `codex/animated-coffee-standby-upstream-3d764f8`
PR: Draft #36 — https://github.com/Babo96/kaffeedisplay/pull/36

## Scope

- Show a tasteful coffee cup with three rising steam strokes on the 480×480 display after the existing administrative dim timeout.
- Reuse the native LVGL cup shapes from the boot splash. Animate three steam wisps with native LVGL lines; no separate standby bitmap or image-generation pipeline.
- Reuse `DisplaySettings.dim_timeout_s`, `brightness_idle` and `screen_off_timeout_s` unchanged.
- Stop steam updates after screen-off; preserve a full-screen wake target so an invisible tap never books a product.
- Keep the existing AP stay-awake behavior and avoid touching user/avatar, accounting and REST contracts.

## Acceptance

- Real moving steam appears at idle, unless the admin disabled dimming or the access point is active.
- Animation pauses after screen-off. First tap dismisses standby and never purchases a drink.
- Existing simulator booking, scrolling and AP tests still pass.
- A deterministic screenshot demonstrates the standby view.
- Host, web, simulator, firmware and size checks succeed on the final branch.
- Physical-device idle dim / wake / power use must be reviewed before merge.

## Evidence

- [Standby screenshot](../ui/screenshots/10-coffee-standby.png) generated from the real LVGL simulator.
- The boot splash and standby share the native LVGL cup shapes in `cup_art.cpp`.
- [PR #36 and current CI](https://github.com/Babo96/kaffeedisplay/pull/36/checks).
