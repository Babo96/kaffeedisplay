# Serial CLI

The USB-C port exposes the CH340 serial console at `/dev/ttyUSB0`. It is a
development, recovery and hardware-test interface; nothing at runtime depends on
it.

## Protocol

- 115200 baud, 8 data bits, no parity, 1 stop bit (8N1), UTF-8.
- Commands are single lines terminated by `\n` (CR is ignored).
- A command reply is one line starting with `OK` or `ERR`.
- Unsolicited ESP-IDF log lines (`I (1234) tag: ...`) may interleave; clients
  ignore everything that does not start with `OK` or `ERR`.
- Replies use `key=value` fields so they stay machine-readable.
- Destructive commands require the admin password (physical access is the
  recovery boundary). The CLI never echoes Wi-Fi or session secrets.

Interactive use:

```sh
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
```

Or set the clock with the helper:

```sh
python3 tools/settime.py                  # host UTC now
python3 tools/settime.py --status
python3 tools/settime.py --epoch 1762819200 --source manual
```

`settime.py` exits 2 when the port is absent, 1 on `ERR`/timeout and 0 on `OK`.

## Commands

| Command | Reply | Purpose |
|---|---|---|
| `help` | `OK help commands=...` | List commands (one line). |
| `status` | `OK status seq=<n> users=<n> users_active=<n> products=<n> products_active=<n> time=<epoch> time_source=<src> time_trusted=<0\|1> ap=<on\|off\|starting> ap_ip=<ip> sta=<on\|off> sta_ip=<ip> uptime_s=<n> heap_free=<n> heap_min=<n> heap_internal_free=<n> heap_internal_min=<n> heap_internal_largest=<n> psram_free=<n> psram_total=<n> lvgl_stack_free=<n> httpd_stack_free=<n> storage_used=<n> storage_total=<n> storage_mounted=<0\|1> firmware=<version> drinks_today=<n>` | Ledger, system and interface summary. `*_stack_free` are task high-water marks in bytes; internal heap min/largest are caps. |
| `ap` | `OK ap state=<on\|off\|starting> ssid=<ssid> [password=<pw>] ip=<ip> remaining_s=<n> sta=<on\|off>` | AP state. `password` is only present while the AP is active. |
| `time` | `OK time <epoch> source=<src> trusted=<0\|1> last_sync=<epoch>` | Read the device clock. |
| `time set <epoch> [source]` | `OK time set <epoch>` | Set UTC seconds; source is `manual`, `browser` or `sntp` (default `manual`). |
| `display test` | `OK display test timeout_s=15` | Show the 15 s rotation/touch crosshair and corner markers. |
| `display info` | `OK display info rotation=<deg> brightness=<pct> active=<pct> idle=<pct> backlight=<0\|1> width=480 height=480 pclk_hz=<hz> hsync_pulse=<n> hsync_back=<n> hsync_front=<n> vsync_pulse=<n> vsync_back=<n> vsync_front=<n>` | Current display settings and RGB timing. |
| `display rotation <0\|90\|180\|270>` | `OK display rotation <deg>` | Change and persist the display rotation. |
| `display brightness <0..100>` | `OK display brightness <pct>` | Persist 5–100%; 0–4 is a temporary diagnostic level (0 = backlight off). |
| `ui tap <x> <y>` | `OK ui tap x=<x> y=<y>` | Test/HIL-only: inject a synthetic press/release at raw panel coordinates through the real LVGL indev/hit-test path. Requires the write unlock. |
| `ui book <user_id> <product_id>` | `OK ui book queued user=<id> product=<id>` | Test/HIL-only: queue `debug_select_user` + `debug_book` on the LVGL task. Requires the write unlock. |
| `ap start` | `OK ap start ttl=<seconds>` | Start the admin AP window (same policy as page 3). |
| `ap stop` | `OK ap stop` | Stop the AP and clear admin sessions. |
| `ap extend [<seconds>]` | `OK ap extend ttl=<seconds>` | Extend the AP window (default 900 s, max 86400 s). |
| `setup-password` | `OK setup-password <password>` | Print the initial admin password until it has been changed. |
| `backup` | `OK backup <bytes>` followed by JSON and `END backup` | Bounded backup export (max 256 KiB); routine backups should use the admin API. |
| `factory-reset <admin_password>` | `OK factory-reset confirm=YES timeout_s=30`, then `YES` → `OK factory-reset` | Two-step destructive reset. Erases ledger, settings and network secrets, then reboots. Requires the current admin password; anything other than `YES` cancels. |
| `unlock-writes <admin_password>` | `OK unlock-writes ttl=600` | Test/HIL only: unlock `book`/`void` for 10 minutes. A build with `HIL_ALLOW_WRITES=1` (CMake option `KAFFEE_SERIAL_ALLOW_WRITES`) has them unlocked from boot. |
| `book <user_id> <product_id> [request_id]` | `OK booking <event_id> <amount_cents>` | Test/HIL-only booking through the same service path as the touch UI. |
| `void <event_id> [reason...]` | `OK void <event_id>` | Test/HIL-only reversal through the service. |
| `reboot` | `OK reboot` | Reboot the device without erasing data. |

`book`, `void`, `ui tap` and `ui book` are locked by default because they create
real ledger events or can drive the UI/AP controls. Unlock them at runtime with
the admin password, or build the firmware with `HIL_ALLOW_WRITES=1`. Read-only
commands never print the admin/setup password unless `setup-password` is used,
and never print the AP password while the AP is off.

`ERR <code> <message>` examples: `ERR not-found user`, `ERR auth required`,
`ERR unknown-command book`. A timeout is not an error reply; clients must fail
with a clear diagnostic instead of retrying destructive commands.

## Hardware tests

`tools/hil.py` uses this protocol for the boot banner and the `book` command.
See `docs/testing.md` for the HIL environment variables and skip conditions.
