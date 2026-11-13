# UI specification

## Device — 480×480 LVGL 9

Horizontal `lv_tileview`, three pages, dots at the bottom. Swipe or tap a page
dot to change pages; product taps never trigger navigation.

### Page 1 — Buchen

- Idle: name tiles with one of 30 distinct static cartoon illustrations (IDs 1–30)
  and an initials-only fallback (ID 0); the name is always shown next to the
  artwork. Optional `Gast · Bar` tile, always the last tile.
- Ordering: join order by default. With the admin setting
  **Reihenfolge der Benutzer** set to `Meistgebuchte zuerst`, the member tiles are
  ordered by their booking count for the current month (most first, ties by
  name, members without a booking last); guest sales never count. An unsynced
  clock keeps the join order. See ADR 0015.
- Sized for 30+ members: the grid stays readable and touchable at that count and
  degrades gracefully beyond it; see `docs/tasks/T-013-page1-layout.md`.
- Empty state: `Keine Benutzer – Admin: Seite 3 → WLAN starten`.
- Selected: name + `Wechseln`, saldo (green credit / red `Offen`), product tiles
  with icon, name and price.
- Tap product → **immediate booking**. No confirmation, no cart, no PIN.
- Toast: `Kaffee · 0,50 € · Saldo 3,50 €`, with `Rückgängig` for 8 s for member
  bookings. Guest sales have no undo.
- Debounce 250–300 ms per tile. Rapid taps book repeatedly.
- Auto-deselect after 30 s idle.
- No connectivity, QR or help content on this page.

### Page 2 — Statistik

- Current month, `<` / `>` arrows for previous/next month.
- Ranking by number of products bought. **No balances, no money, no prices.**
- Top 3 highlighted; bars; scrollable. Guest count shown separately in the footer.
- Rows draw the same avatar as page 1 (preset motif, otherwise coloured initials),
  looked up from the user record, so archived members keep their motif in past months.
- Counts are net of voids within the period: purchases booked in the month add
  one, voids recorded in that same month subtract one. A void recorded in a
  later period does not change the original month's count.
- Time-quality warning if the clock is not synced.

### Page 3 — System

- WLAN card: status, `WLAN starten` (3 s long-press), countdown, `Verlängern`,
  `Stoppen`. QR codes only while AP is active: Wi-Fi join + URL. If only STA is
  up, URL QR for the LAN address.
- Time card: current time, source, last sync, quality, `Zeit synchronisieren`
  (starts AP if needed).
- Device stats: chip temperature, uptime, free heap, free PSRAM, storage,
  firmware, MAC, IP/RSSI, drinks today.
- Display: read-only rotation/brightness/timeouts. Admin changes them in the web.
- No balances or financial totals.

### Theme

Dark espresso: background `#1A1412`, surface `#241B18`, text `#F5EFE6`, accent
`#D4A373`, positive `#8FBC8F`, negative `#E07A5F`. Rounded 20–24 px cards, large
fonts (Inter/Montserrat subset with umlauts and €), high contrast, big targets.

## Admin web — German, mobile-first

- Login (single `admin`).
- Übersicht: Kassenstand, Guthaben, offene Beträge, Differenz, Verbrauch,
  Gastumsatz, Einzahlungen, Ausgaben; alerts (time, debts, backup, storage).
- Benutzer: create/edit/archive, balance, payment (`Einzahlung`), adjustment,
  history, and a 30-motif avatar picker. New users get a random preset by
  default, avoiding the previous new user's preset. Admins can manually choose
  a preset or initials-only (ID 0); existing users keep their saved choice.
  No upload or additional image decoder is required.
- Getränke: add/edit/reorder/archive/delete, category, price, icon, colour.
- Ausgaben: add/edit/void, categories, vendor, note.
- Kassensturz: counted cash, expected, difference, history.
- Berichte: week/month/year/all/custom, tabs (Übersicht, Verbrauch,
  Einzahlungen, Ausgaben, Kassenfluss, Salden, Transaktionen), CSV Excel/raw.
- Einstellungen: display rotation/brightness/timeouts/test pattern, AP/STA,
  time/timezone, guest on/off, debt limit, password.
- System: device stats, AP control/QR, logs, backup/restore, OTA, factory reset.
- No external fonts/CDNs; bundle everything.

## Screenshots

For any user-facing change, update this specification, the visible examples in
`README.md`, and the generated screenshots in the **same branch**. Do not
claim a new UI state is documented until the screenshots demonstrate it.

The simulator must produce deterministic screenshots into
`docs/ui/screenshots/` (page 1 idle, page 1 selected, page 2, page 3 with AP/QR,
admin dashboard, reports). They are committed and embedded in the top-level
`README.md`. Regenerate the device set with `make screenshots`; `make sim` is the
interactive loop. See `sim/README.md` and
`firmware/components/ui/README.md` for the firmware integration seam.

## Strings

All user-visible strings are German and centralised:

- Device: `firmware/components/ui/src/strings.hpp`
- Web: `web/src/i18n/de.ts`

Code identifiers, comments, logs and docs remain English.
