# Task board

One task per worktree/branch. `main` is protected. Update the row and the task
file as you work; put the PR link under Evidence when done.

| ID | Title | Track | Depends | Owner | Status |
|---|---|---|---|---|---|
| T-001 | Domain, storage, service implementation | backend | foundation | backend-agent | done |
| T-002 | Admin SPA: all pages + German UI | web | foundation | web-agent | done |
| T-003 | Board bring-up + 3-page LVGL device UI | embedded | foundation | board-agent / ui-agent | done |
| T-004 | HTTP API, auth, AP/time, backup/OTA | backend | T-001 | net-agent / api-agent | done |
| T-005 | Reports + Excel CSV backend | backend | T-001 | reports-agent | done |
| T-006 | LVGL simulator + screenshots + README | ui | T-003 | ui-agent | done |
| T-007 | HIL tools, fonts/assets, release pipeline | qa | foundation | tooling-agent | done |
| T-009 | Firmware integration, reports, factory reset and boot | integration | T-001..T-008 | integration-agent | done |
| T-010 | Serial console CLI + live AP validation | tooling | T-009 | serial-cli-agent | done |
| T-011 | Rotated display rendering (90/180/270) | embedded | T-010 | – | mitigated (0° enforced; rotated flush open) |
| T-012 | Optional user avatars (30 offline illustrations) | ui/web/domain | T-001, T-002, T-003 | 42vibes | done ([#14](https://github.com/Babo96/kaffeedisplay/pull/14)) |
| T-013 | Order booking-page users by consumption (admin toggle) | ui/web/domain | T-004, T-012 | – | done ([#18](https://github.com/Babo96/kaffeedisplay/pull/18)) |

Design record for T-013: [T-013-page1-layout.md](T-013-page1-layout.md) (page 1
for 30+ members — grid geometry, scroll affordance and the numbers behind them).

## Workflow

1. Claim a task: set Owner, status `in-progress`, create the worktree.
2. Implement + test (`make check` or the relevant subset).
3. Update docs/OpenAPI/ADR in the same branch.
4. Push, open PR, request review. Do not merge your own PR without review.
5. After merge, set status `done` and add the PR link.

## Status values

`ready`, `in-progress`, `review`, `blocked`, `done`.
