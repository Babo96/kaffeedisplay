# T-002 — Admin SPA: all pages + German UI

Status: review
Track: web
Owner: web-agent
Branch: `feat/T-002-web`

## Context

`web/` has a working Vite/React/TS/Tailwind shell, API client, mock server,
German strings and a dashboard skeleton. This task builds the complete admin UI
against `docs/api/openapi.yaml` and the mock API.

## Pages

- Login (single `admin`), session handling, time sync on login.
- Übersicht: summary cards, alerts (time, debts, backup, storage), recent events,
  period chart, quick actions.
- Benutzer: list/search, create/edit/archive, balance, topup, adjustment,
  history, debt highlighting.
- Getränke: CRUD, category, price, icon/color picker, sort, archive/delete.
- Ausgaben: CRUD, categories, vendor, note, void.
- Kassensturz: expected vs counted, difference, history.
- Berichte: period selector (Woche/Monat/Jahr/Gesamt/Custom), tabs (Übersicht,
  Verbrauch, Einzahlungen, Ausgaben, Kassenfluss, Salden, Transaktionen), JSON
  tables and CSV Excel/raw downloads.
- Einstellungen: display rotation/brightness/timeouts/test pattern, AP/STA,
  time/timezone, guest toggle, debt limit, password.
- System: device stats, AP status/QR, logs, backup/restore, OTA, factory reset
  (with confirmations).
- Mobile-first, dark espresso theme, no CDN, German only.

## Requirements

- Use `request()` from `src/api/client.ts`; CSRF and request IDs are automatic.
- Send browser time on login (`syncTimeFromBrowser` already wired).
- Loading/empty/error states, optimistic disabled buttons while saving.
- No balances on device-related admin? Admin may see all; device pages don't.
- Bundle budget: total gzip < 400 KB.
- Unit tests for money formatting and critical components; Playwright e2e for
  login + create user + topup + report CSV.

## Files

`web/**`, `tests/e2e/**`, `docs/api/openapi.yaml` (only if a contract gap is
found — coordinate first).

## Acceptance

- `npm --prefix web run typecheck`, `test`, `build` green.
- `make e2e` green against the mock (if Playwright is set up).
- All strings German; no hardcoded English.
- Works at 360 px and desktop.
- Screenshots attached to PR.

## Evidence

- PR: https://github.com/Babo96/kaffeedisplay/pull/6
- Screenshots: `docs/ui/screenshots/web/web-*.png`, regenerated with
  `npm --prefix web run screenshots` (starts mock + Vite itself).
- Unit: `npm --prefix web test` — 26 tests (money parse/format incl. junk
  input, Europe/Berlin DST periods, report summaries, CSRF refresh, QR,
  StatCard, BarChart).
- E2E: `CI=1 npm --prefix web run e2e` — 9 Playwright tests (login/logout,
  create/archive/restore user, topup, custom-period CSV filename, product CRUD
  with full `ProductInput` assertions, expense void, display + AP start/stop
  with QR, untrusted-time warning, 360 px no horizontal overflow). Each test
  resets the mock through the mock-only `POST /api/v1/__test/reset` route.
- Build: `npm --prefix web run build` — 120.0 KiB gzip total (budget 400 KiB).
- `make check` green locally; firmware build skipped without `idf.py` (CI job).

## Contract assumptions

- `PATCH /products/{id}` sends the full `ProductInput` (name, category,
  price_cents, icon, color, sort, active); the mock rejects partial bodies.
- `POST /auth/login` may omit `csrf_token`; the SPA always refreshes the
  session and token through `GET /auth/me` before the first write.
- `ApStatus.wifi_qr` / `url_qr` are plain payload strings; the SPA renders them
  locally with `qrcode-generator@2.0.4` (ADR 0011) and still accepts
  `data:image/*`.
- Report JSON shape is frozen in `docs/api/reports.md` (envelope,
  `columns`/`rows`, `sections`, flat scalars, `totals`, `time_quality`).
- Factory reset has no OpenAPI route yet, so the danger-zone button is disabled
  and labelled “Werkseinstellungen (noch nicht verfügbar)”. The SPA keeps a
  guarded `POST /api/v1/factory-reset` path ready for T-004.
