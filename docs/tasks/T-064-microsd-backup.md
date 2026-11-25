# T-064 — MicroSD as backup storage (DB backup + daily monthly CSV)

Status: in-review
Track: firmware / storage
Owner: implementation lane (fixer)
Branch: `feat/64-microsd-backup`
Issue: [#64 — Micro-SD als Backup-Speicher](https://github.com/Babo96/kaffeedisplay/issues/64)
PR: [#65](https://github.com/Babo96/kaffeedisplay/pull/65)

## Scope

The TF slot becomes backup-only storage. It holds `kaffeekasse-db.json` (the
exact portable `GET /api/v1/backup` payload) and the current month's
`kaffeekasse_YYYY-MM_transactions.csv`. The card is never live storage: each run
mounts, writes and unmounts. No compatible card means no action.

Out of scope: a new UI page, `SystemStats`/`/device`, the OpenAPI contract, and
restoring from the card.

## Decisions

- New component `firmware/components/sdbackup/`; `storage` stays the ledger's
  only writer and does not grow FatFs/SPI/report dependencies. See
  [ADR 0018](../adr/0018-microsd-backup.md).
- Pure, host-testable planning core with injected `IVolume`/`IStateStore`.
  ESP adapter, NVS state and the FreeRTOS task are compiled only under
  `ESP_PLATFORM`.
- Daily run at **00:08 Europe/Berlin**, polled every 45 s. Before 00:08 the
  previous day's 00:08 is the reference. A last CSV success before the current
  month start writes the previous month first, then the current month.
  Verified boundary decisions:
  - 1st, 00:08, last run 30th 00:08 → previous CSV, current CSV, DB.
  - 1st, 00:05, last run 30th 00:08 → previous CSV only (current run is not due
    yet, DB marker already current).
  - 25th, 00:05, last run 24th 00:08 → nothing (the 24th run counts for the day).
- DB backup is clock-independent; CSV requires `TimeSource::Synced`. `Unset`
  and `Restored` never produce a CSV (no `kaffeekasse_1970-01_…`).
- `backup_to_json()` is shared by `export_backup()` and the SD task. The SD task
  builds it from `ledger_snapshot()` + secret-free `settings()` so SD I/O never
  runs under the service mutex. A host test asserts equality modulo
  `created_at` and the absence of secrets.
- Success markers live in NVS namespace `kaffee_sd` (`last_db`, `last_csv`) and
  only advance after `fflush` + `fsync` + `fclose`. Separate markers let a
  DB-only run (untrusted clock) repeat the CSV once the clock is trustworthy.
- Writes are streamed through `reports::Sink`; no whole CSV document is
  buffered. The DB document is the existing export.
- Card format: MBR + FAT32, 4–32 GB, 16–32 KiB clusters; no exFAT, no GPT. The
  device never formats (`format_if_mount_failed = false`).
  `CONFIG_FATFS_LFN_HEAP=y` is required for the CSV filename.
- Mount uses SPI2, `max_files=4`, `allocation_unit_size=16 KiB`,
  `disk_status_check_enable=true`; internal pull-ups on CS (42) and MISO (41).
  Teardown always attempts unmount + `spi_bus_free()`.
- The task starts after the boot health check/OTA confirmation, priority 2,
  5120-byte internal stack. Mount-failure logs are rate-limited to once per
  15 minutes.
- **Status for page 3:** `SystemStats` gains additive `sd_card_present`,
  `sd_backup_ok`, `sd_last_success` and `sd_last_error` (defaults = not
  detected). `main` owns a `SdStatusMonitor`; the backup task writes it and
  `PlatformAdapter::stats()` copies `snapshot()` under its mutex. No SD I/O in
  the render/stats path. Serial `status` prints the new fields; the sim fake
  reports a healthy card. `/device`, OpenAPI and web are untouched — page 3
  layout/strings belong to the designer.
- **Hot swap:** with no run due, a 5-minute mount/unmount probe refreshes
  `sd_card_present` without writing. A probe changes only presence; it never
  touches `backup_ok`, `last_success` or `last_error`. A due run's mount counts
  as the probe. Missing-card logs share the 5-minute probe rate limit.
- **Size budget:** the base image was 3,097,840 B with 47,888 B free. The SD
  stack (FatFs, SDMMC/SPI, sdbackup) adds ~110 KB. The firmware UI only uses
  object/button/label/line/image/qrcode/tileview and initialises no LVGL theme,
  so the unused widgets, themes, grid layout, observer, examples and demos are
  disabled in `sdkconfig.defaults`. The image is now 3,124,704 B (21,024 B
  headroom).

## Evidence

- `make test-host`: 181 test cases / 27,776 assertions pass, including the new
  plan truth table, month-boundary order, failed-write retry, mount failure,
  missing card, untrusted clock, probe interval/presence rules, the
  status-monitor snapshots and the DB export equivalence/no-secrets test.
- `npm --prefix web test`: 40 tests pass; `npm --prefix web run typecheck`:
  clean.
- `PATH=/tmp/cfbin:$PATH make fmt-check`: clean with clang-format 18.
- `PATH=/tmp/cfbin:$PATH make check`: green. Firmware build is skipped locally
  because `idf.py` is not installed; the Docker build below covers it.
- Firmware Docker build (`espressif/idf:release-v5.5`):
  `kaffeekasse.bin` 3,124,704 B, `python3 tools/check_sizes.py` all budgets ok
  (3051.5 KiB / 3072 KiB).
- **Not verified on hardware**: no card and no `/dev/ttyUSB0` were available.
  The physical mount, card removal while mounted and brown-out behaviour remain
  to be checked on the device.
- **Page 3 status word** (designer): `SD` plus one state word at the clock
  baseline inside the 56 px header — `SD fehlt` / `SD erkannt` (muted), `SD ok`
  (positive colour), `SD Fehler` (negative). No timestamp, no pill, no border;
  the last success stays in the serial log. One label, re-texted by the existing
  page-3 refresh (immediate on page entry, 5 s throttle) from cached `stats()`
  only. Measured on the committed capture: the longest token `SD erkannt` is
  102 px wide (ink x 298–399), the gap to the clock is 16 px and the title ends
  at x 108, leaving 189 px slack at 480 px. Pixel diff against `main`: only rows
  24–37 / columns 347–399 differ (`SD ok`); every row ≥ 45 is identical, so the
  WLAN card, both QR codes with their quiet zone and captions and the page dots
  render exactly as before. `docs/ui/screenshots/05-system-ap-qr.png`
  regenerated; `make test-ui` and `tools/check_screenshots.py` cover the page.

## Risks

- Brown-out or card removal mid-write can truncate a file or corrupt the card's
  FAT. Nothing reads the files at runtime; the next run overwrites the same
  names, but the card may need a PC-side `fsck`.
- exFAT/GPT cards are silently ignored. Documented in `docs/operations.md`.
- GPIO 41/42 are JTAG pins and the board's external pull-ups are unverified;
  internal pull-ups are enabled before mounting.
- The 47/48 sharing with panel init is safe today. Any future runtime panel
  re-init must unmount the card and free the SPI bus first.
