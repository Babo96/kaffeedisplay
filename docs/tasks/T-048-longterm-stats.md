# T-048 — Long-term statistics

Status: in-progress
Track: backend + web
Owner: agent T-048
Branch: `feat/48-longterm-stats`

## Context

Ticket https://github.com/Babo96/kaffeedisplay/issues/48 asks for long-term
statistics such as cups per user and per product over the whole ledger, not just
the current report range. The data already exists in the append-only event
history; this task exposes it as an additive read model.

## Scope

1. Domain: `Ledger::long_term_stats()` — pure, host-testable aggregation of
   member and guest purchases per user and product, net of voids. A void is
   attributed to the original booking (same policy as the leaderboard).
2. Reports: `reports::long_term_months()` — the same purchases grouped by
   Europe/Berlin calendar month, again net of voids in the original month.
3. Service: `KaffeeService::long_term_stats()` under the service mutex.
4. API: read-only `GET /api/v1/stats/longterm` with the existing session auth
   and `time_quality` envelope; documented in `docs/api/openapi.yaml`.
5. Web: German admin page **Langzeit** with per-user, per-product and per-month
   tables, honest empty/loading/error states and centralised strings.
6. Mock server + unit tests.

## Non-goals

- No device UI change; pages 2/3 stay free of financial totals.
- No ledger mutation, no new persistence, no new dependency.
- No CSV export; reports remain the export path.

## Decisions

- Voids are not deleted or filtered by timestamp. They subtract from the
  original booking's user/product/month, matching `Ledger::leaderboard()` and
  the consumption report.
- `spend_cents` is the positive consumed amount, never a balance. Balances stay
  derived from the ledger.
- `estimated_items` counts sales whose `time_source` is not `synced`, net of
  voids, so the UI can flag estimated timestamps without hiding data.
- Empty users/products are included with zero counts (mirroring the
  leaderboard), but the page hides the tables entirely when no sale exists.

## Files

- `firmware/components/domain/include/kaffee/domain.hpp`
- `firmware/components/domain/src/domain.cpp`
- `firmware/components/domain/include/kaffee/json.hpp`
- `firmware/components/domain/src/json.cpp`
- `firmware/components/reports/include/kaffee/reports.hpp`
- `firmware/components/reports/src/reports.cpp`
- `firmware/components/service/include/kaffee/service.hpp`
- `firmware/components/service/src/service.cpp`
- `firmware/components/api/src/api_handlers.cpp`
- `firmware/components/api/src/api_routes.cpp`
- `docs/api/openapi.yaml`
- `web/src/pages/LongTermPage.tsx`
- `web/src/pages/LongTermPage.test.tsx`
- `web/src/api/{types,endpoints}.ts`
- `web/src/{App.tsx,i18n/de.ts,components/Shell.tsx}`
- `web/mock/server.mjs`
- `tests/host/{domain,reports}_test.cpp`

## Evidence

- PR: https://github.com/Babo96/kaffeedisplay/pull/56
- Host tests: `make test-host` — 161 cases / 27615 assertions passed
- `make check` — fmt-check, OpenAPI lint, docs, host + web tests, typecheck,
  web build, asset budgets all passed; firmware build skipped (no `idf.py`)
- Web tests: `npm --prefix web test` — 37 tests passed
- Typecheck/build: `npm --prefix web run typecheck`, `npm --prefix web run build`
  passed
- Docker firmware build not run (no Docker daemon assumption); CI covers it.
