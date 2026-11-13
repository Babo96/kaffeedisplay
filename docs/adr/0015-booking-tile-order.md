# ADR 0015: Order the booking tiles by consumption

Status: Accepted

## Context

Page 1 lists the member tiles in join order. A fund with more members than fit
on one 480 × 480 screen (the deployment target is 30+ members) makes the
members who book several times a day scroll past members who joined recently.
The administrator wants the regulars first, switchable from the admin web UI,
and the default must not change how existing devices look.

The ordering could be done in the ledger, in the service, or in the UI. It is a
presentation concern: the same leaderboard data already exists for page 2, and
nothing about balances, the journal or the backup schema changes.

## Decision

- **One new persisted setting** in `AppSettings`: `user_sort` with the values
  `usage` and `creation`. `usage` is the default: the owner's requirement is that
  the members who book most sit first, so a fresh device and a device that never
  saved settings both come up sorted; `creation` restores the join order and
  stays one click away in the admin UI. It is part of the settings blob, `GET/PUT /settings`,
  backup export/import, and the admin **Einstellungen** page. Unknown spellings
  are rejected by a live `PUT` and fall back to the stored value when read from
  an older or hand-edited blob.
- **Ordering lives in the UI layer** (`ui::order_members()`), not in the ledger.
  `UiAppImpl::ordered_members()` fetches counts from
  `IUiBackend::leaderboard(month_start, month_end)` — the same source page 2
  renders — so the tile order and the leaderboard cannot disagree. Ties break by
  name, members without a booking this month sink to the end, and the guest tile
  keeps its fixed position. `creation` mode does no extra work at all.
- **The period is the current month (Europe/Berlin).** It rewards current habits
  and reuses the existing month-bounds helper. A rolling window can be added as
  another enum value later without touching the wire format.
- **No clock, no reordering.** The month only needs a plausible wall clock, so a
  browser-synced kiosk (`time_trusted=0`, `source=browser`) still sorts; only an
  unset clock (`now == 0`) keeps the join order.
- **The page-1 catalog fingerprint covers the order.** It iterates the members
  in display order, so a re-order rebuilds page 1 and an unchanged order does
  not — the same trap `avatar_id` hit in ADR 0014.
- Page 1's layout is sized for 30+ members (see `docs/tasks/T-013-page1-layout.md`):
  more members per screen and a larger LVGL task stack, because a rebuild
  allocates every tile at once.

## Consequences

- Reordering costs one leaderboard scan per data change when `user_sort=usage`;
  `creation` keeps the previous cost exactly.
- A booking can change the order, so with `usage` page 1 rebuilds once per
  booking (deferred through the existing queue, never inside a click callback).
- Nothing changes for API clients that omit `user_sort`, and a backup without
  the key loads as `creation`.
- The admin dashboard's own user list keeps its management order; only the
  device booking page follows the setting.
