# T-012 — Optional preset user avatars

Status: done
Track: ui/web/domain
Owner: 42vibes
Branch: `feat/user-avatars` (from `main` 3c1c626, PR #14)

## Scope

- 30 distinct offline cartoon bitmap illustrations (animals, objects and coffee motifs), paired with visible initials on device.
- New users receive a server-assigned random preset by default (excluding the immediately preceding new user's preset); explicit IDs or initials-only (0) remain selectable by admin.
- Persist avatar IDs (0..30) using compatible user events.
- Update REST/OpenAPI, tests, ADR and documentation.
- Leave booking, money and balances unchanged.

## Acceptance

- Current users and old journal hashes work with avatar 0.
- Admin can select, save, clear and restore the avatar, and default new-user POSTs receive a random 1–30 preset.
- A stale user PATCH that does not mention avatar does not clear it.
- IDs outside 0..30 are rejected and never committed.
- Admin web, simulator and ESP-IDF firmware compile; host/API tests pass.
- UI screenshot evidence accompanies the upstream pull request.

## Evidence

- Simulator seed now displays four preset avatars and the initials-only fallback.
- README and UI specification updated with instructions and preview references.
- Screenshot artifacts generated from the changed simulator and mock UI, rather than screenshots copied from `main`.
- Screenshot verification and CI results must pass before marking this task done.

## Evidence

- `make test-host`: 148 cases / 27,514 assertions pass, including the new
  contract tests for `-1` (unspecified), `0` (initials), archive-preserves and
  out-of-range rejection.
- `make check`, `npm --prefix web run typecheck`, `npm test`, the Playwright
  suite and `make screenshots-check` pass; the simulator CI job regenerates the
  device frames and compares them.
- `tools/check_avatar_i4.py` is now part of `make check`, so a local green
  cannot hide a CI failure on the I4 layout.
- Hardware: boot health `nvs=1 storage=1 display=1 lvgl=1 service=1 ui=1
  network=1 api=1 => ok` with the I4 artwork linked; internal heap and LVGL
  stack watermarks reported by `status`.
- Admin review fixes: the page-1 catalog fingerprint now includes `avatar_id`
  (an admin avatar change reaches the panel), the plate is baked to the card
  surface colour, the initials badge is gone, random assignment prefers unused
  presets, and the picker previews/re-rolls the random choice.

## Follow-ups

- Five motifs read ambiguously at 32-46 px (7 Pinguin, 9 Igel, 14 Schaf,
  18 Marienkäfer, 29 Berge) and three grey/black faces are easy to confuse
  (4 Panda, 8 Waschbär, 15 Koala). Redrawing them is art work, not a code
  change; tracked separately.
- The leaderboard page still draws coloured initials, so a user with a preset
  looks different on pages 1 and 2.
