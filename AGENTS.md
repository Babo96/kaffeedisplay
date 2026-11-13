# AGENTS.md — Kaffeekasse

This file is the canonical guide for every human or AI contributor. Read it
before touching code. Read `docs/architecture.md`, `docs/data-model.md` and your
role file in `docs/agents/roles/` next.

## Mission

A 100% self-contained coffee fund on a Guition ESP32-4848S040. A trust-based
touch kiosk for booking drinks and a German admin web UI served by the device
itself. No cloud, no CDN, no runtime internet dependency.

## Golden rules

1. **Runtime is offline-first.** Never introduce a mandatory network service,
   CDN, telemetry endpoint or cloud API.
2. **Money is `int64_t` cents.** Never use floating point for money.
3. **Balances are derived** from ledger events. Never store a balance as truth.
4. **Never delete ledger history.** Corrections and deletions of bookings are
   reversals (`void`) with an audit trail. Products/users are archived or
   snapshotted.
5. **One writer per module.** Business writes go through `KaffeeService`; it
   serializes them and owns persistence. UI and API never write storage directly.
6. **German UI only.** Device and admin UI strings are German. Code comments,
   commit messages, docs and identifiers are English. Keep strings centralised
   (`ui/strings.hpp`, `web/src/i18n/de.ts`).
7. **No PINs on the device.** Booking is trust-based. Only the single admin
   account authenticates (web).
8. **Contracts before implementations.** `docs/api/openapi.yaml`,
   `components/domain/include/kaffee/domain.hpp`,
   `components/service/include/kaffee/service.hpp` and
   `components/ui/include/kaffee/ui.hpp` are frozen interfaces. Changing them
   requires an ADR and a PR that updates all consumers.
9. **`make check` must pass** before a PR is opened. CI enforces it.
10. **No secrets, no backups, no generated assets in git.**

## Quick start

```sh
make bootstrap      # toolchain check + npm install
make test           # host unit tests + web tests
make build          # ESP-IDF firmware build (local idf.py or Docker)
make flash          # /dev/ttyUSB0
make monitor        # serial console
make sim            # LVGL SDL simulator
make web-dev        # Vite dev server + mock API
make check          # format, lint, tests, builds, size budgets
```

Useful environment variables: `IDF_PATH`, `ESPPORT` (default `/dev/ttyUSB0`),
`IDF_DOCKER_IMAGE` (default `espressif/idf:release-v5.5`).

## Repository map

```
firmware/
  main/                 app entry, sdkconfig defaults
  components/
    board/              display, touch, backlight, power
    domain/             pure C++ ledger + SHA-256 (host-testable)
    storage/            LittleFS journal, snapshots, NVS settings
    service/            KaffeeService: serialization, idempotency, events
    ui/                 LVGL 9 pages, theme, German strings
    net/                AP+STA, captive DNS, mDNS, SNTP, time service
    api/                esp_http_server, auth, REST, static SPA, CSV, OTA
web/                    admin SPA (React + TypeScript + Vite, German)
sim/                    LVGL SDL simulator (same ui + domain code)
tests/host/             doctest unit/property tests for domain + storage
tests/api/              contract tests against mock or device
tests/e2e/              Playwright admin UI tests
tools/                  bootstrap, flash, HIL, asset/font generation, backups
docs/                   architecture, ADRs, task board, role prompts
```

## Interfaces (do not break without an ADR)

| Interface | File | Consumers |
|---|---|---|
| Domain types + Ledger | `firmware/components/domain/include/kaffee/domain.hpp` | storage, service, tests |
| Service facade | `firmware/components/service/include/kaffee/service.hpp` | ui, api |
| UI entry | `firmware/components/ui/include/kaffee/ui.hpp` | main, sim |
| Network/time | `firmware/components/net/include/kaffee/net.hpp` | service, api, ui |
| Admin REST | `docs/api/openapi.yaml` | web, api, tests |
| Display settings | `firmware/components/domain/include/kaffee/settings.hpp` | ui, api, board |

## Task workflow

1. Pick an unclaimed task from `docs/tasks/README.md`.
2. Create a worktree from `main`:
   `git worktree add -b feat/T-xxx-short ../kaffeedisplay-wt/T-xxx main`
3. Update the task file: owner, branch, status `in-progress`.
4. Implement, test, and update docs/OpenAPI in the same branch.
5. Run `make check`.
6. Commit with Conventional Commits (`feat:`, `fix:`, `docs:`, `test:`,
   `refactor:`, `chore:`).
7. Push the branch, open a PR with the template, and request review from
   another agent/human.
8. Only merge after CI is green and the diff has been reviewed. Squash merge.
9. Mark the task `done` with a link to the PR.

Never push directly to `main`. Never force-push a shared branch. Keep PRs small
(< ~400 changed lines where practical) and scoped to one module.

## Definition of done

- Behaviour implemented and covered by tests (domain: unit/property; API:
  contract; web: component/e2e for core flows).
- German UI strings, no hardcoded English in user-visible text.
- Docs updated (`docs/api/openapi.yaml` for API changes, ADR for contract or
  architecture changes, task file status).
- `make check` green.
- No new dependency without an ADR and a pinned version.
- Screenshots/gifs for UI changes (simulator is fine).
- No unrelated reformatting.

## Testing without hardware

- `make test` — domain/storage host tests (doctest, ASan/UBSan).
- `make sim` — LVGL SDL simulator, mouse = touch, seeded demo data.
- `make web-dev` — admin SPA against the mock API from the OpenAPI spec.
- `make e2e` — Playwright against the mock API.
- Hardware-in-the-loop: `make hil` when the board is at `/dev/ttyUSB0`.

## Hardware facts (verified)

- ESP32-S3, 8 MB octal PSRAM, 16 MB flash, CH340 USB-serial at `/dev/ttyUSB0`.
- 480×480 IPS, ST7701S RGB565, init sequence type 9.
- GT911 at I²C 0x5D, SDA 19 / SCL 45, no reset/INT wired — poll it.
- Backlight GPIO 38 (LEDC).
- Display pins: DE 18, VSYNC 17, HSYNC 16, PCLK 21;
  R 11/12/13/14/0, G 8/20/3/46/9/10, B 4/5/6/7/15;
  init SPI CS 39 / SCK 48 / MOSI 47.
- GPIO 0 and 3 are strapping pins used as RGB data; do not repurpose.
- USB is development/recovery/power only. Runtime must survive days offline.
- No battery-backed RTC. Time is synced from the admin browser, optionally SNTP.

## Never do

- Commit `sdkconfig`, build output, `web/dist`, `node_modules`, backups or secrets.
- Add an always-on AP or captive portal. AP is demand-activated only.
- Put connection/QR/help instructions on the device booking screen.
- Display balances or financial totals on pages 2/3.
- Hard-delete ledgers or mutate historical event fields.
- Use dynamic allocation inside LVGL render callbacks or hot loops.
- Add a dependency with an unpinned version or an incompatible license.
- Rewrite another agent's uncommitted work in a shared worktree.
