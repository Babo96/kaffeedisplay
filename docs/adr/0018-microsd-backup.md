# ADR 0018: MicroSD as backup-only storage

Status: Accepted

## Context

The Guition ESP32-4848S040 has a microSD (TF) slot on SPI2 (SCK 48, MOSI 47,
MISO 41, CS 42). `docs/hardware.md` previously said the slot was unused because
the pins were believed to conflict with the RGB bus. SCK/MOSI are shared with
the ST7701's 3-wire **init** bus, which is only driven during `init_panel()`;
after that the pins are free. The ledger must remain on LittleFS: the card is
removable, has no card-detect pin, can brown out, and must never become a
second source of truth.

The kiosk needs an offline backup that does not require the admin to open a
browser: a copy of the portable database export and a monthly transaction CSV
that a treasurer can open in Excel.

## Decision

### Backup-only, never live storage

- The card holds exactly two kinds of files: `kaffeekasse-db.json` (fixed name,
  overwritten) and `kaffeekasse_YYYY-MM_transactions.csv` (one file per month,
  overwritten on every daily run). Both live in the card root.
- No ledger write path goes through the card. There is no read at boot, no
  fallback when LittleFS is full, no cache and no restore from the card.
- Every run mounts, writes, and unmounts. The volume is never left mounted.

### Separate `sdbackup` component

The new component is deliberately **not** part of `storage` (the ledger's only
writer) or `reports`. `storage` must stay free of FatFs, SPI and ESP-IDF report
dependencies, and the SD policy must be host-testable without a card. The
component contains:

- a pure planning/orchestration core (`sd_backup.cpp`) with injected volume and
  state interfaces,
- an ESP-only FatFs/SPI adapter (`sd_card_esp.cpp`),
- an ESP-only NVS state store (`nvs_sd_state.cpp`),
- a low-priority FreeRTOS task (`sd_backup_task.cpp`).

No domain, service, UI or API contract changes. There is no new JSON schema:
the DB file is byte-for-byte the existing `GET /api/v1/backup` document
(`backup_to_json()` is the shared implementation; `export_backup()` calls it
under the service mutex). Secrets are excluded by the existing
`settings_to_json(..., false)`.

### Schedule and boot catch-up

- The daily run is at **00:08 Europe/Berlin**, not midnight. DST changes happen
  at 02:00–03:00, so 00:08 is never ambiguous. A 30–60 s poll means a browser
  time jump across 00:08 is still noticed.
- A run at or after 00:08 writes the current month's CSV and then the DB
  backup. Before 00:08, yesterday's 00:08 is the most recent scheduled run.
- If the last CSV success predates the start of the current month, the
  **previous** month's CSV is written first, then the current month. On the 1st
  at 00:08 this finalises the old month and starts the new one in the same run.
- The DB backup is clock-independent and may run when the clock is unset or
  restored. The CSV is written only when `TimeSource::Synced`; `Restored` is
  stale after an outage and `Unset` has `now == 0`. This prevents
  `kaffeekasse_1970-01_transactions.csv`.
- Success markers are stored in the private NVS namespace `kaffee_sd`, one for
  the DB and one for the CSVs. They are written only after a successful close
  (`fflush` + `fsync` + `fclose`). A failed write leaves the marker untouched,
  so the next poll retries the same day.

### Card format policy

- ESP-IDF 5.5 FatFs supports FAT12/16/32 but **not exFAT**. The partition table
  must be **MBR**; GPT cards are not mounted. Recommended cards are 4–32 GB
  pre-formatted as FAT32 with 16–32 KiB clusters. Larger cards ship as exFAT
  and will be ignored.
- Long file names are off by default in IDF. `CONFIG_FATFS_LFN_HEAP=y` in
  `sdkconfig.defaults` is required for `kaffeekasse_YYYY-MM_transactions.csv`.
- The device never formats a card: `format_if_mount_failed = false`. A mount
  failure is only logged and the card is left untouched.
- Mount parameters: `max_files = 4`, `allocation_unit_size = 16 KiB`,
  `disk_status_check_enable = true`. Teardown is `unmount` followed by
  `spi_bus_free()`, also after a failed mount or a write error.

### Failure behaviour and visibility

- No card, exFAT/GPT card, or a removed card: nothing happens. Mount failures
  are logged at most once per 15 minutes, not per 45 s poll.
- A write error is treated like card removal: abort the remaining writes,
  attempt an unmount, keep the success marker, retry later.
- Status is visible only through the log ring (`/logs`) and the serial console.
  `SystemStats`, `/device` and the OpenAPI document stay unchanged; a UI/status
  surface would be a separate ADR.

### Status to the device UI (scope addition)

- `SystemStats` gains four additive fields with safe defaults:
  `sd_card_present`, `sd_backup_ok`, `sd_last_success` and `sd_last_error`.
  This is an additive change to the frozen header; `/device`, the OpenAPI
  document and the web admin are deliberately **not** changed.
- `main` owns one `SdStatusMonitor`. The backup task writes it; the
  `PlatformAdapter` takes a reference and copies its `snapshot()` into
  `SystemStats::stats()`. This keeps the dependency direction `main → sdbackup`
  and avoids a `sdbackup → service` cycle or a new virtual on `IPlatform`.
- `SdStatusMonitor` guards its state with a mutex because the UI/LVGL task and
  the serial console read it while the backup task writes it. `snapshot()`
  returns a value copy; no SD I/O happens in the render or stats path.
- Defaults mean "no card detected / no successful run yet". The backup task
  updates the monitor only after a successful write or a failed attempt, so the
  stale case is "last observed state", not a per-frame probe.
- When no run is due, a mount/unmount pair every **5 minutes** refreshes
  presence without writing. The run's own mount counts as the probe for that
  interval, so the card is never mounted more often than the probe interval.
- **Probe status rule:** a successful probe sets only `card_present = true`; a
  failed probe sets only `card_present = false`. Neither touches `backup_ok`,
  `last_success` or `last_error`, so a failed probe never erases the result of
  the last real run or replaces a write error with a mount error. Only a real
  run updates those three fields.
- Missing-card log lines share one rate limit with run-time mount failures:
  at most one per probe interval (5 minutes).

## Consequences

- The admin keeps a current portable backup without logging in, and the
  monthly CSV is available even if the device is offline for weeks.
- The card can be removed at any time without affecting bookings. The worst
  case is a missed backup run, which the next poll repeats.
- Brown-out while writing can leave a truncated file; the next successful run
  overwrites it with the same fixed name. Nothing reads those files at runtime.
- Backup and restore remain a browser/API flow. The SD card is **not** part of
  the restore path; a restore still goes through `POST /api/v1/backup/restore`.
- FAT32 has no journal. Pulling the card while it is mounted, or losing power
  mid-write, can corrupt the card's FAT; the device does not repair or format
  it. Documented in `docs/operations.md`.
- External pull-ups on CS/MISO are unverified. Internal pull-ups are enabled
  before mounting; if cards are unreliable, inspect the board.
- The FatFs/SDMMC/SPI stack adds ~110 KB to an app image that was already near
  the 3 MiB partition limit. The device UI only uses object/button/label/line/
  image/QR/tileview widgets and initialises no LVGL theme, so the unused
  widgets, themes, grid layout, observer, examples and demos are disabled in
  `sdkconfig.defaults`. Without that trim the build does not fit; re-enable a
  widget only together with a new size check.

## Alternatives considered

- **Use the card as a second ledger store.** Rejected: removable media has no
  reliable write ordering, no card-detect ISR and no hash-chain recovery path;
  it would become a second source of truth.
- **Write the CSV at midnight.** Rejected: 00:00 is adjacent to DST transitions
  in some zones and gives no safety margin for a poll-based scheduler.
- **Mount on every poll and keep the volume open.** Rejected: the card must be
  safely removable, and a mounted FAT volume keeps file handles and the SPI bus
  claimed.
- **Format unknown cards on the device.** Rejected: destroys user data and can
  not be undone.
- **Add a status field to `/device`.** Rejected for this step: the contract is
  frozen and the log ring already carries the state.
