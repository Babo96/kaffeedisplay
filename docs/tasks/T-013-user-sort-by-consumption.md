# T-013 — Order the booking-page users by consumption

Status: done
Track: ui / web / domain
Owner: –
Branch: `feat/T-013-firmware` (PR #18)
Issue: https://github.com/Babo96/kaffeedisplay/issues/16
Depends on: T-004 (booking page), T-012 (consumption data already available via the leaderboard)

## Problem

Page 1 lists the active users in creation order. In a fund with more users than
fit on the 3-column grid, the members who book several times a day have to
scroll or swipe for their own tile, while members who joined yesterday sit on
the first row.

## Goal

The member with the most bookings in the current period appears first, the
second most second, and so on, so recurring users do not have to scroll. The
behaviour is switchable by the administrator in the admin web UI and is off by
default (today's order stays the default).

## Scope

1. **Setting.** `AppSettings` gains an explicit sort mode (not a bare bool, so a
   third order can be added later): `user_sort = creation | usage`, default
   `creation`. Persisted with the other app settings, validated on import,
   exposed in `GET/PUT /settings` and in `docs/api/openapi.yaml`, shown as a
   select/switch in the admin **Einstellungen** page with German copy, and
   applied on the device without a reboot (the settings change already notifies
   the UI).
2. **Ordering.** When the mode is `usage`, page 1's user grid is sorted by
   booking count for the **current month (Europe/Berlin)**, descending, ties
   broken by name ascending so the order is stable. Counts come from
   `IUiBackend::leaderboard(month_start, month_end)`, the same source as page 2,
   so the tile order and the leaderboard never disagree. Guest sales stay out of
   the count, and voided bookings are already netted by the ledger.
3. **Guest tile.** The guest tile keeps its fixed position at the end of the
   grid; only the member tiles are ordered.
4. **Refresh.** The page-1 catalog fingerprint must include the resulting order,
   otherwise re-sorting after a booking never reaches the panel (the same trap
   that PR #10/#14 hit with `avatar_id`). A re-sort therefore rebuilds page 1
   once per booking when the mode is `usage` — deferred through the existing
   queue, never from inside a click callback.
5. **Fallbacks.** If the leaderboard lookup fails or returns an empty period,
   fall back to the current order instead of blanking the grid. Archived users
   are never shown.

## Acceptance

- `user_sort = usage`: a user with more bookings this month is rendered above a
  user with fewer, and the order matches page 2's ranking for the same period.
- `user_sort = creation` (default): byte-identical page 1 to `main`'s current
  behaviour; existing devices are unaffected after upgrade.
- Toggling in the admin UI changes the device order without a reboot, and the
  value round-trips through backup/restore and `GET/PUT /settings`.
- Invalid/unknown values are rejected by the API and fall back to `creation`
  when read from an old or corrupt settings blob.
- German strings only in the admin and on the device; no new dependency.
- Host tests for the ordering and the settings round-trip, a web component test
  for the control, and the simulator screenshot set regenerated.

## Open questions

- Period: current month (proposed, matches page 2) vs. all time vs. a rolling
  window. Current month is the default proposal; switching the window should be
  a follow-up, not part of this task.
- Should the admin dashboard user list follow the same setting? Proposal: no —
  the dashboard is a management view and keeps its own order.

## Evidence

## Evidence

- `make test-host`: 154 cases / 27,524 assertions, including the ordering rules
  (ties by name, members without a booking last, creation order untouched) and
  the settings round-trip with its lenient read path.
- `make check`, `npm typecheck`, `npm test` (35), Playwright (11), `make
  screenshots-check` (8 frames) pass; CI green on `main` (`a27502f`).
- Device, flashed from the CI artifact: `boot health ... => ok`, 15 members,
  `rotation=0`, `lvgl_stack_free=18256` with the 24 KiB stack, `psram_free` up by
  ~450 KiB because the third rotation buffer is gone. `display rotation 90`
  answers `ERR rotation-unsupported`.
- Page 1 geometry and the pinned guest action are recorded in
  `T-013-page1-layout.md` (16 readable tiles, 4 × 106 × 80 px, scrollbar only
  when the list overflows).
- The admin control (`Zufällig`-style segmented choice under *Kasse*) saves
  `user_sort` through the existing `PUT /settings` path and falls back to
  `creation` when the device omits the field.
