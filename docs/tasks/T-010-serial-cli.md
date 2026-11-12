# T-010 — Serial CLI and live AP validation

Status: done
Track: tooling / integration
Owner: T-010 serial-cli agent
Branch: `feat/T-010-serial-cli` (from `main` 4cd69bb)

## Context

`docs/serial-cli.md` and `tools/settime.py`/`tools/hil.py` documented a line
protocol, but the firmware had no console task. Recovery, `settime` and HIL write
tests therefore could not run against a real board.

## Deliverables

1. `firmware/main/serial_console.{hpp,cpp}`: bounded UART0 line reader
   (115200 8N1, 512-byte limit, CR ignored), replies only via `OK`/`ERR` lines.
   The task never calls LVGL; commands go through `KaffeeService`/`IPlatform`.
2. Commands: `help`, `status`, `ap`/`ap start|stop|extend`, `setup-password`,
   `time`/`time set`, `backup`, `reboot`, `factory-reset`, and the HIL-only
   `book`/`void` plus `unlock-writes`.
3. Secrets: `status` never prints AP/admin secrets; `ap` prints the AP password
   only while the AP is active; `setup-password` prints only while the first-run
   password is still valid. Command lines are not logged.
4. Destructive reset: current admin password + explicit `YES` within 30 s;
   erases ledger/settings/network NVS and reboots to first boot.
5. HIL: `book`/`void` are locked by default and unlocked for 10 minutes with
   `unlock-writes <admin_password>` or at build time with
   `HIL_ALLOW_WRITES=1` / `KAFFEE_SERIAL_ALLOW_WRITES=ON`. `tools/hil.py`
   unlocks automatically in write mode.
6. Docs updated (`docs/serial-cli.md`).

## Live validation (real board, /dev/ttyUSB0)

- Flash + console: `status` reported storage mounted, heap/PSRAM, firmware,
  AP/STA, time quality; `ap` printed SSID/password/IP/remaining while active;
  `setup-password` returned the first-run password.
- `wlan0` joined `Kaffeekasse-B834`; login with the setup password returned
  `must_change_password=true`; forced change to `Kaffee-Test-2026` returned 204;
  the next login had `must_change_password=false`.
- Created a temp user/product, topped up 500 ct, checked `/users`,
  `/stats/summary`, `overview`/`consumption` `csv_excel` (BOM + `sep=;` + CRLF),
  `/device` storage/backup fields, `/backup` (no secrets), `/` real SPA and
  `/generate_204`/`/hotspot-detect.html`/`/connecttest.txt`/`/ncsi.txt`.
- `unlock-writes` + `book` created `evt_254f324dcbfe98b4` (`drinks_today=1`);
  `void` restored the user to 500 ct and the report to 0 sales/1 Storno;
  archived the user and deleted the product.
- `factory-reset` cancellation (`no`) left data intact; the confirmed reset
  replied `OK factory-reset`, rebooted with `time=0 source=unset`, no users, a
  new setup password and a new AP password, and the provisioning AP active.
- `make hil` read-only passed against the live AP.

## Follow-up live fixes

1. **`ap stop` deadlock.** Tracing showed `stop_ap()` holding the network mutex
   while calling `mdns_free()`; the mDNS teardown waits for the Wi-Fi/event task,
   which was blocked on that same mutex. mDNS teardown is now scheduled on the
   network timer task (outside the mutex), with an `mdns_stopping` guard so a
   concurrent AP start either keeps the instance or restarts it after the
   deferred free. `ap stop` returns promptly, `status` works immediately after,
   and `ap start` recovers. This also removes the deadlock from
   `NetworkManager::factory_reset_credentials()`. The API listener/session clear
   remains deferred through `ApiController::on_ap_stopped()`.
2. **`drinks_today`.** `KaffeeService::stats()` now counts only non-voided
   `Purchase`/`GuestSale` events in the current Europe/Berlin day (correct DST
   boundaries). A host test covers purchase → 1 and void → 0; live serial and
   `/device` both showed 1 then 0.
3. **HTTP task stack overflow (found during validation).** Creating a product
   through the API overflowed the `httpd` task at 10 KiB and rebooted the board.
   The server stack is 12 KiB; the ineffective
   `CONFIG_HTTPD_TASK_STACK_SIZE` line was removed from `sdkconfig.defaults`.


## Display scanout fix (follow-up)

The panel image was shifted right by ~55–65 px (UI left clock/content clipped,
right edge wrapped). The simulator was correct, and the type-9 test crosshair
shifted with the panel, confirming scanout rather than layout. HBP/HFP/pulse
sweeps only moved the phase by a few pixels; 16 MHz made it worse. The root
cause is a PSRAM RGB FIFO underrun: the RGB DMA occasionally fetches a line
late, so the panel latches the next line offset and wraps.

Fix: five-line internal-RAM bounce buffer (`bounce_buffer_size_px = 5 * 480`)
with the PSRAM framebuffer still enabled, plus the known-good Guition timing
`12 MHz, HSYNC 8/50/10, VSYNC 8/20/10`. Measured crosshair shift dropped from
+72.4 px to about +8 px; page dots are centred and page 1 has no edge clipping.
The HTTP server stack is 12 KiB to make room for the 16 KiB LVGL and 12 KiB
serial-console stacks; product creation was re-tested through the API (201, no
panic). LVGL uses the C library allocator instead of its 64 KiB internal pool,
Wi-Fi uses eight static RX buffers, and mDNS lives in PSRAM. `status` shows the
internal heap and LVGL stack high-water so this can be watched.
Final webcam frames: `/tmp/opencode/t010-display/final-test.jpg`,
`/tmp/opencode/t010-display/final-page1-bounce5.jpg`.


## UI booking crash (follow-up)

Selecting a user and then tapping a beverage crashed/rebooted the device while
`book` over serial stayed healthy. Reproduction through the new
`ui book <user_id> <product_id>` command at the old 8 KiB LVGL task stack
produced:

```text
***ERROR*** A stack overflow in task taskLVGL has been detected.
Backtrace: ... |<-CORRUPTED
Rebooting...
```

Root causes fixed:

1. `esp_lvgl_port` task stack was 8192 bytes; the full page refresh after a
   booking used about 9.4 KiB. Raised to 16384 bytes. `status` now reports
   `lvgl_stack_free`; after 10+ bookings/refreshes it settles around 6488 bytes
   free.
2. Selecting/clearing a user rebuilt page 1 synchronously from inside the
   card/button click callback, so `lv_obj_clean()` deleted the object currently
   dispatching the event. `select_user()`, `select_guest()` and
   `clear_selection()` now only update state and `queue_.push()`; the existing
   housekeeping timer performs the rebuild on the next LVGL tick.
3. The booking hot path now updates the visible balance and toast in place and
   suppresses the service change callback once, so it does not rebuild page 1.
   External topups/voids update the balance in place when a catalog fingerprint
   shows that users/products did not change; hidden stats/system pages are
   marked dirty and refreshed when entered. This stopped the repeated
   object-churn/internal-heap drain seen under serial `void` stress.
4. The final memory split is: 16 KiB LVGL stack, 10 KiB console stack (static
   internal), 16 KiB HTTP stack, five-line RGB bounce buffer, LVGL CLIB malloc
   with all allocations preferring PSRAM, Wi-Fi 4 static RX buffers, mDNS
   task/memory in PSRAM. This boots, starts the AP/HTTP server with >5 KiB
   HTTPD headroom, survives 10+ UI bookings, and keeps `status`/flash commands
   working.

Diagnostics: `ui tap <x> <y>` injects a press into the real LVGL indev callback;
`ui book <user_id> <product_id>` queues `debug_select_user` + `debug_book` on
the LVGL task. Both were exercised without a panic.


## Page 3/report abort (follow-up)

Repeated `GET /reports/transactions?...` and leaving the UI on page 3 aborted in
newlib while creating a `FILE` lock:

```text
abort -> lock_init_generic -> __retarget_lock_init_recursive -> __sfp
_fopen_r -> fopen -> JournalEventStore::file_size -> info()
KaffeeServiceImpl::stats() -> UiAppImpl::update_system_dynamic
```

The LVGL housekeeping timer polled `stats()` every second; `stats()` called
`JournalEventStore::info()`, whose `fopen`/`fclose` allocated newlib FILE locks
from the tiny internal heap until it aborted.

Fixes:

- `JournalEventStore::info()` now uses `stat()` for file sizes/existence; no
  `FILE` or lock allocation in the hot path. The host `FileEventStore` uses the
  same base implementation; LittleFS totals still come from
  `esp_littlefs_info()`.
- `KaffeeService::stats()` caches storage/last-backup/drinks for 3 s and
  invalidates the cache on mutations, backup export and factory reset.
- Page 3 dynamic updates run only while page 3 is visible and at most every
  5 s; hidden pages are marked dirty and refreshed on entry. Housekeeping no
  longer touches the system page while another page is visible.
- Host regressions call `JournalEventStore::info()` and
  `KaffeeService::stats()` 5000 times and check storage values/fd count.

Hardware evidence: 30× JSON + 10× CSV reports all returned 200; page 3 was held
for ~10 minutes with `status` high-water stable (`heap_internal_free` 603–623 B,
`lvgl_stack_free` 10088 B, no abort/reboot). UI `ui book`/`ui tap` still books;
test events were voided afterwards.


## PR #10 review follow-up

- **B1:** `JournalEventStore::info()` no longer uses `fopen`/`fclose`; file
  sizes use `stat()`. `stats()` caches storage/backup/drinks for 3 s, page 3
  dynamic data refreshes only while visible and at most every 5 s, and
  `next_request_id()` no longer reads stats. A >10-minute page-3 soak kept
  `heap_internal_free` at ~139.5 KiB (`min` ~106 KiB, `largest` ~61 KiB) with
  no abort/reboot.
- **B2:** `ui tap`/`ui book` now require `unlock-writes` and use the same
  write gate as `book`/`void`; docs and the console header were corrected.
- **B3:** HTTPD high-water is exposed as `httpd_stack_free`. At 12 KiB it fell
  to 1476 B after settings writes, so the stack was restored to 16 KiB and RAM
  recovered from Wi-Fi RX buffers/console stack. With 16 KiB: 30× JSON + 10×
  CSV reports, 10× backup, 5× settings PUT and a full 2.8 MB OTA write (SHA
  mismatch, no boot switch) left `httpd_stack_free=5608 B` (~5.6 KiB headroom
  on the 16 KiB stack).
- **B4:** `tileview_changed_cb()` now consumes `stats_dirty_`/`system_dirty_`
  and a swipe to Statistik after 10 bookings showed Nathy 10 / Babo 1 (updated
  leaderboard, no stale page).
- Nits: catalog fingerprint includes `guest_enabled`; it is recorded
  unconditionally; in-place balance labels update colour; `suppress_change_`
  was removed (callback always queues, fingerprint avoids rebuild); `lv_async_call`
  failure returns `ERR ui-queue`; `inject_touch` returns false before UI ready;
  `status` reports internal heap min/largest, LVGL and HTTPD high-water.

## Evidence

- `make test-host`: 141 cases / 27,448 assertions pass (includes 5000×
  `JournalEventStore::info()` / `KaffeeService::stats()` no-leak regressions).
- `make check`: format, OpenAPI lint, docs links, host/web tests, web build,
  assets and size budgets pass.
- `make build-docker` (ESP-IDF 5.5.6): app image 0x2b6cc0, 10% partition free.
- `make flash` on `/dev/ttyUSB0`; boot log confirms storage, UI, network,
  HTTP API, serial console ready, health check ok.
- Live serial `ap stop` → `OK ap stop`, immediate `status` (AP off), `ap start`
  → `OK ap start ttl=900`, `status` (AP on); repeated after final flash.
- Live `book` → `drinks_today=1` in serial and `/device`; `void` →
  `drinks_today=0` in both. Product creation via API returned 201 in 0.12 s with
  no panic after the stack fix.
- API `POST /ap/stop` dropped the connection (expected) but the next `status`
  worked and the old session returned 401 after AP restart.
- Final factory reset left `seq=0`, `users=0`, `products=0`, `time=0
  source=unset`, AP provisioning up with fresh setup/AP passwords.
- Live curl/serial transcripts captured in `/tmp/opencode/t010*` during the run
  (secrets redacted; final setup/AP values reported to the owner).
