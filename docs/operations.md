# Operations

## First boot / provisioning

1. Device has no users and no network. Page 1 shows `Keine Benutzer`.
2. Page 3 → `WLAN starten` (3 s long-press). First boot auto-starts the AP for
   15 minutes as a provisioning convenience.
3. Join `Kaffeekasse-XXXX` with the QR or password shown on page 3.
4. Open `http://192.168.4.1` (or scan the URL QR).
5. Log in with the initial admin password shown on page 3 (`Erste
   Einrichtung`); the web UI forces a password change before anything else.
6. Create members and products; optionally configure STA Wi-Fi.
7. Stop the AP (or let it time out). The device returns to offline operation.

The AP is never on permanently. Captive portal support is best-effort; the QR
and manual IP are the reliable paths.

## Samsung / Android / iPhone connection notes

These are admin-only concerns and are not shown on the booking screen.

- Samsung One UI may say “No internet” and auto-switch to mobile data. Disable
  “Switch to mobile data” in Intelligent Wi-Fi, or choose “Stay connected”.
- Private DNS / Secure DNS may bypass the captive DNS hijack. Use the manual URL
  `http://192.168.4.1` if the portal does not appear.
- Browser “HTTPS only” mode must be off for the local HTTP page.
- iOS usually shows a “Sign in to network” sheet; if not, open the URL manually.
- If the device has an STA uplink, use the LAN address and skip the AP entirely.
  Optional NAPT can give AP clients internet so phones keep the connection.

## Time

- The admin browser sets time on login and every ~5 minutes. A manual
  `Zeit synchronisieren` button exists in System settings.
- SNTP is used if STA has internet.
- With USB attached, `python3 tools/settime.py` sets the clock over the serial
  CLI (see `docs/serial-cli.md`).
- If the device is powered but offline for days, time stays accurate from the
  last sync minus small crystal drift.
- If power is lost, the clock is restored as `restored`/`unset`. Bookings still
  work; reports flag estimated timestamps until the next admin sync.
- An RTC module or LiPo is optional hardware for better offline time; not v1.

## Network / API integration

- `INetworkListener::on_network_state_changed()` and `on_ap_stopped()` are
  delivered from the network timer task, never from the Wi-Fi event task or an
  HTTP task. The API must hand start/stop and session cleanup to its own
  task/queue instead of blocking the callback.
- `on_ap_stopped()` fires once after a confirmed AP stop; this is the hook for
  clearing admin sessions and stopping the HTTP server.
- AP passwords and QR payloads are masked while the AP is inactive.

## Backup

- Admin → System → Backup downloads a versioned JSON file (no secrets).
- Restore validates the file and writes a new snapshot; keep a copy before
  restoring.
- Recommended: download a backup after large changes and before OTA.
- The device warns in the dashboard when the last backup is older than 30 days.

## MicroSD backup

The TF slot is an automatic, backup-only target. It is never used as live
storage, is not read at boot, and is not part of the restore path.

### Preparing a card

1. Use a 4–32 GB card. Larger cards usually ship as exFAT and will not mount.
2. Partition it with an **MBR** partition table (no GPT) and format the
   partition as **FAT32** with 16–32 KiB clusters. On Linux:
   `sudo mkfs.vfat -F 32 -s 32 /dev/sdX1` after an MBR partition table
   (`parted /dev/sdX mklabel msdos`).
3. Do not use exFAT, NTFS or APFS. ESP-IDF 5.5 FatFs supports FAT12/16/32 only.
4. Insert the card; no button press or reboot is needed. The device checks
   every 30–60 s.
5. The card is never formatted by the device, even if it cannot be mounted.

### Files

| File | Contents |
|---|---|
| `kaffeekasse-db.json` | The same portable export as `GET /api/v1/backup` (no passwords, no Wi-Fi credentials). Fixed name, overwritten. |
| `kaffeekasse_YYYY-MM_transactions.csv` | The current month's transactions in the Excel CSV format of the report export. Rewritten on every daily run. |

The daily run is at **00:08 Europe/Berlin**. The DB backup is also refreshed
then. If the device was off at 00:08, it catches up after boot once the clock is
trustworthy; on the 1st it finalises the previous month before writing the new
one. A CSV is never written while the clock is unset or only restored, so no
1970 file appears.

### Removing the card

- Unplugging the card is safe for bookings; the card is only open during a run.
- No reboot is needed to insert or remove a card. Presence is refreshed by a
  mount/unmount probe within about 5 minutes when no backup run is due; while
  idle nothing stays mounted.
- If the card is removed mid-write, the run aborts, the device tries to unmount
  and retries later. The partially written file is overwritten by the next
  successful run.
- The device logs a missing/incompatible card at most once per 5 minutes. Use
  `/logs` or the USB serial console to check.
- The SD card is **not** a restore source. To restore, upload the JSON through
  Admin → System → Backup/restore as usual.
- A card formatted with GPT or exFAT is ignored. Reformat it as MBR + FAT32 on a
  PC.

## OTA

- Admin → System → Firmware upload (`firmware.bin` from CI/release).
- Dual partition, SHA-256 check, boot health check, automatic rollback.
- USB flashing is the recovery path: `make flash`.
- Keep the device on a stable 5 V supply during OTA.

## USB / offline periods

- USB is not required at runtime. The device can run for days without network.
- Serial console: logs, `status`, `time set`, `backup`, `factory-reset`. The full
  line protocol and HIL-only commands are documented in `docs/serial-cli.md`.
- If unplugged from power, it stops; time is restored/resynced on next boot.
- Journal compaction and storage-full handling work offline. When storage is
  critically full, new bookings are rejected with a clear message; admin must
  export/compact.

## Build, assets and release

- `make web` builds the admin SPA. `make assets` embeds `web/dist` as
  deterministic gzip and raw data in the `webassets` component; generated files
  stay out of git. `make build` runs both.
- Device fonts are committed by the UI track in
  `firmware/components/ui/src/fonts/` (Montserrat subsets with German umlauts,
  `€`, digits and punctuation, generated once with `lv_font_conv@1.5.3`). The
  ESP-IDF build must not need Node; their README documents regeneration.
- `make check` runs format, OpenAPI lint, relative-link checks, host/web tests,
  the web build, asset generation and size budgets. On a machine without
  `idf.py` it skips the firmware build; CI/Docker builds the firmware.
- Releases are built by `.github/workflows/release.yml`. The trigger is a
  **published GitHub release** (or a manual `workflow_dispatch` with an existing
  `v*` tag); a tag push on its own publishes nothing. The job builds the tagged
  commit, verifies the images and attaches to that release: `firmware.bin` (OTA
  app image), `firmware-merged.bin` (flash at offset `0x0`), `SHA256SUMS`,
  `manifest.json` (commit, IDF image, web asset sizes, checksums) and a
  changelog. Host tests run in the release job.

### Flashing a release

`firmware-merged.bin` contains the bootloader, the partition table, the OTA data
and the app, so it is the whole device in one file:

```sh
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 write_flash 0x0 firmware-merged.bin
```

The flash parameters come from the image headers (`sdkconfig.defaults`: 16 MiB,
QIO, 80 MHz). The merged image ends before `0x620000`, so the `storage`
partition with the ledger is not touched: flashing a release keeps users,
products and bookings. Use `firmware.bin` instead when updating over OTA from
the admin UI.

The release build stamps the tag as `PROJECT_VER`, so `GET /health` and the
admin UI report the release version (`v1.0.0`) instead of the ESP-IDF default
`1`. The workflow refuses to upload an app image whose version is not the tag.
- `make hil` runs the hardware smoke test when a board is at `/dev/ttyUSB0`;
  see `docs/testing.md`.

## Hardware smoke test

```sh
make flash
make hil                                        # read-only
HIL_ALLOW_WRITES=1 HIL_ADMIN_PASSWORD='...' make hil
```

The script does not reset the board or start the AP unless requested with
`--reset` / `--start-ap`. In write mode it creates a temp user/product, books
through the serial `book` command, tops up, voids through the serial `void`
command and exports a report CSV. Cleanup **voids the test events and archives**
the temp user/product; the append-only audit trail remains. It skips clearly
when the serial port is absent; if the board runs different firmware the boot
check fails instead of pretending the test passed.

## Recovery

- **Admin password lost**: serial CLI reset (physical access) or factory reset;
  commands are in `docs/serial-cli.md`.
- **AP won't start**: check page 3 status/logs; reboot; USB serial diagnostics.
- **Display black**: verify power, then USB logs; ST7701 init depends on board
  revision. Keep the last known-good rotation in NVS.
- **Corrupt journal**: device boots from the last valid event and quarantines the
  tail; export logs and restore a backup if needed.
- **Boot loop**: after 3 crashes, AP-only diagnostics mode starts.
- **Factory reset**: erase the `kaffee_net` NVS namespace (AP credentials and
  last known time). `NetworkManager::factory_reset_credentials()` performs this
  in-process and regenerates the AP password; otherwise erase the namespace as
  part of the storage factory reset.

## Display settings

- Rotation and brightness are set in the admin web and applied live.
- If a rotation makes the screen unusable, revert from the web (network is
  unaffected). The device returns to page 1 after a change.
- Active/idle brightness and timeouts persist in NVS.
- While the AP is active, the QR screen stays at active brightness.
