# Hardware

## Board

**Guition ESP32-4848S040** (also sold as JC4848W540).
Verified over USB: ESP32-S3 QFN56 rev v0.2, 8 MB octal PSRAM, 16 MB flash,
CH340 USB-serial at `/dev/ttyUSB0`, MAC `f8:5b:1b:90:b8:34`.

| Part | Detail |
|---|---|
| MCU | ESP32-S3R8, dual-core LX7 @ 240 MHz, 512 KB SRAM |
| PSRAM | 8 MB octal, 80 MHz |
| Flash | 16 MB, quad, 3.3 V |
| Display | 4.0" IPS 480×480, ST7701S, RGB565 parallel |
| Touch | GT911 capacitive, I²C 0x5D |
| Backlight | GPIO 38, LEDC PWM |
| USB | USB-C, CH340C serial (not native USB) |
| Extras | microSD (not used), battery connector, optional relays |

Do **not** use the relay version to switch mains. Relays are ignored.

## Display pins (ST7701S, RGB565, type-9 init)

| Signal | GPIO |
|---|---|
| DE | 18 |
| VSYNC | 17 |
| HSYNC | 16 |
| PCLK | 21 |
| R0–R4 | 11, 12, 13, 14, 0 |
| G0–G5 | 8, 20, 3, 46, 9, 10 |
| B0–B4 | 4, 5, 6, 7, 15 |
| CS (init only) | 39 |
| SCK (init only) | 48 |
| MOSI (init only) | 47 |
| RESET | not connected |
| Backlight | 38 |

GPIO 0 and 3 are strapping pins used as RGB data. They are outputs after boot;
do not repurpose. The panel stays black with any init sequence other than the
ST7701 type-9 sequence. Verify against the board revision at bring-up.

RGB timing verified against Arduino_GFX/ESPHome and live scanout measurements:
PCLK 12 MHz, HSYNC pulse 8 / back porch **50** / front porch 10, VSYNC pulse
8 / back porch **20** / front porch 10, DE active high, PCLK not inverted. Use
the exact Arduino_GFX `st7701_type9_init_operations` table for the 480×480
Guition panel; the generic `esp_lcd_st7701` default sequence is not compatible.
The table includes `0xCD 0x00` (disable the MDT test pattern) and ends with
`0x3A 0x60`; otherwise the panel can show a blue field with a narrow colour-bar
band instead of the framebuffer.

The 480 px line is **shifted right and wrapped** if the RGB FIFO underruns
while DMA reads the PSRAM framebuffer. Timing/porch sweeps alone only changed
the phase by a few pixels. The fix is a small internal-RAM bounce buffer:
`rgb_config.bounce_buffer_size_px = 5 * 480` (five lines, ~9.4 KB for the two
buffers) with the PSRAM framebuffer still enabled. With the bounce buffer the
type-9 test crosshair lands within ~7 px of centre instead of ~55–65 px right;
the remaining offset is below the camera/panel tolerance and does not clip the
page content. Do not remove the bounce buffer when changing PCLK or porches.

## Touch (GT911)

| Signal | GPIO |
|---|---|
| SDA | 19 |
| SCL | 45 |
| INT | not connected |
| RST | not connected |
| Address | 0x5D |

Poll at ~100 Hz. There is no reset or INT line, so recovery after a failed probe
re-creates the GT911 driver; a hardware reset is not possible. No calibration
needed. Touch coordinates are transformed for display rotation 90/180/270 (LVGL
applies the same rotation formula to pointer input).

## Power and thermals

- 5 V via USB-C, ~260 mA typical. Use a 5 V/2 A supply to avoid brownout with
  Wi-Fi + full backlight.
- Optional LiPo on the board connector keeps it alive during short power gaps;
  it does not provide days of runtime.
- Chip temperature sensor is the ESP32-S3 internal die sensor — approximate,
  not ambient.

## Storage

- 16 MB flash, custom partition table:
  NVS 24 KB, otadata 8 KB, phy 4 KB, two 3 MB OTA slots, 9 MB LittleFS.
- microSD is not used; pin conflicts with the RGB bus make it unreliable.
- Journal/snapshot wear is handled by append-only writes + LittleFS wear
  levelling.

## USB / offline behavior

- USB is development, recovery and power only. Runtime must not depend on it.
- The device must run for days without network: journal compaction, watchdog,
  time-quality flags and backups-on-demand all work offline.
- If power is lost, time is unknown until the admin browser syncs; see
  `docs/operations.md`.

## Rotation / brightness

- **Rotation 0/90/180/270 is applied in the framebuffer blit**, not by the panel
  or by LVGL. The panel has no transpose hardware (`esp_lcd`'s RGB `swap_xy` is a
  mask consumed only by the CPU-copy branch, and the ST7701 vendor driver never
  writes MADCTL `MV`), and LVGL's software renderer ignores matrix rotation.
  Since the board already blits each band from the internal-RAM draw buffer into
  the panel framebuffer, it rotates the pixels on the way: 0° is a row memcpy,
  180° reverses, and 90/270 transpose. The mappings are the inverse of
  `lv_display_rotate_point()`, so touch follows the picture at every angle.
  Cost: a transpose is slower than a memcpy (measured next), so a page switch at
  90/270 is heavier than at 0°.
- Because direct full-frame flushes keep the LVGL task busy, the board also
  polls the real indev from an explicit 30 ms LVGL timer (`lv_indev_read()`);
  the port's implicit indev timer alone is not serviced reliably in this mode.
- `lvgl_port_remove_disp()` is not used: with `avoid_tearing`, the LVGL draw
  buffers are the panel's two scanout FBs, and the port would free them. The
  display lives for the firmware lifetime; destructor stops the port task.
- Backlight: LEDC 19 kHz, 12-bit, gamma-corrected. 20 kHz at 12-bit is not
  reachable from the 80 MHz APB clock (it needs a divider below 1), so 19 kHz is
  used. Two on states: active and idle/dim. Screen-off after timeout; touch
  wakes. QR screen stays awake while the AP is active via the board
  `set_stay_awake(true)` hold.
