# T-013 — Page 1 for 30+ members (layout decision record)

Status: decision record for T-013, implementation pending in `page_book.cpp`
Track: ui
Owner: design
Supersedes: nothing. Depends on: T-012 (avatars), T-013 (sort order, being
implemented in parallel)

This record decides how the booking page (`Seite 1 — Buchen`) is laid out when
the fund grows past 30 members. It is a design document: **the code change lands
in the T-013 implementation PR**, which owns
`firmware/components/ui/src/page_book.cpp`. The numbers below were measured in
the simulator on a throwaway build of that file (see
[Verification](#verification)) so the implementer can drop them in without
re-deriving them.

---

## 1. Problem, measured on today's code

`page_book.cpp` currently renders a `LV_FLEX_FLOW_ROW_WRAP` grid of fixed tiles:

| Property | Value (measured) |
|---|---|
| Screen | 480 × 480 |
| Header | 0–56 (title + clock) |
| Question label `Wer trinkt?` | 56–96 (40 px band, `font_title` 22 px) |
| Grid content width | 448 px (16 px page padding each side) |
| Columns | 3 at `lv_pct(31)` → **138 px** wide |
| Tile height | **100 px** |
| Gap (column and row) | 8 px → **row pitch 108 px** |
| Tile internals | pad 8, avatar 42, pad_row 5, name `font_body` 18 |
| Body | height 424, y 56–480, pad top 8 / bottom 46 |
| Page dots | overlay band y 450–476 |

Visible member tiles on a 480 px screen, from the simulator at 15 members
([06-page1-15-users.png](../ui/screenshots/06-page1-15-users.png)):

- Full rows start at y = 96, 204, 312 → **3 rows = 9 members fully readable**.
- The 4th row starts at y = 420 and is 60 px tall before the screen edge, so the
  avatars are cut in half and **the names are never visible**. The page dots
  (450–476) then sit on top of those half tiles.
- 15 members = 16 tiles (incl. guest) = 6 rows, content 694 px, scroll range 270 px.
- 30 members = 31 tiles = 11 rows, content 1234 px, scroll range **810 px** (1.9
  screens; measured: capturing at 810 px or 900 px yields the same frame).

So today 9 of 15 members are visible (60 %) and 9 of 30 (30 %). The clipped
fourth row is the only hint that the list continues, and it is unreliable: it
shows faceless half-avatars, and the dots overlay it.

The owner's requirement — "the display should be designed for 30 users plus" —
therefore is not met by tuning the current numbers. Columns and rows both have
to change.

---

## 2. What "designed for 30+" means (budget)

The 480 × 480 panel is 101.6 mm diagonal, 71.8 mm per side → **6.7 px per mm**.
That converts the usual guidelines into pixels for this panel:

| Guideline | mm | px here |
|---|---|---|
| Minimum touch target (Apple 44 pt / Material 48 dp) | 7–9 | 47–60 |
| Comfortable target for a moving finger | 9–12 | 60–80 |
| Finger width (contact patch) | ~10 | ~67 |

Hard limits that follow from this:

- 30 tiles in one screen would need 30 × 48 px of touch area. With a 5- or
  6-column grid this is arithmetically possible (6 × 5 at 74 × 44 px) but every
  tile would then be avatar-only with no readable name, at 44 px = 6.6 mm —
  *below* the finger width. **Rejected**: a kiosk where you guess your tile from
  a wall of 30 faces is slower than one short swipe, and a mis-tap silently
  selects the neighbour.
- Readable names (`font_body` 18 px) need ≥ 88 px of tile width for 9–10
  characters. That caps the grid at **5 columns** (5 × 84 px + gaps) — and at
  84 px the name column is 68 px ≈ 7 characters. **Rejected**: German first
  names like `Katharina` or `Sebastian` would be truncated in a fund where
  several members share a first letter.

So the target is not "all 30 at once" but: **as many fully readable names as
possible, with an unambiguous signal that the list continues.**

---

## 3. Decision — grid geometry

**4 columns × 106 × 80 px tiles, 8 px gaps, grid content 448 px.**

```
16 px │ 106 │ 8 │ 106 │ 8 │ 106 │ 8 │ 106 │ 16 px         = 4·106 + 3·8 = 448
```

| Property | Current | **Decision** |
|---|---|---|
| Columns | 3 | **4** |
| Tile | 138 × 100 | **106 × 80** |
| Row pitch | 108 | **88** |
| Tile internals | pad 8, avatar 42, pad_row 5, name 22 | **pad 5, avatar 42, pad_row 5, name 22 = 79 ≤ 80** |
| Body | h 424 (56–480), pad 8/46 | **h 392 (56–448), pad 8/8** |
| Grid rows starting at | 96, 204, 312, 420 | **96, 184, 272, 360** |
| Full rows visible | 3 (9 members) | **4 (16 members)** |
| 15 members + guest | 16 tiles = 6 rows, scroll 270 px | **16 tiles = 4 rows, fits on one screen, scroll 0** |
| 30 members + guest | 11 rows, content 1234 px, scroll 810 px | **8 rows, content 712 px, scroll 336 px** |
| 60 members + guest | 21 rows, content 2314 px, scroll 1890 px | **16 rows, content 1416 px, scroll 1040 px** |

Why 80 px is the right height (not 92 and not 76):

- **92 px / 3 rows**: taller cards, but 12 members visible instead of 16. At 30
  members that is 40 % vs 53 % reachable without scrolling, for 12 px of extra
  card padding — not worth it.
- **76 px / 4 rows + peek**: 4 rows *and* a 48 px slice of row 5 would fit, but
  the tile internals (5 + 42 + 5 + 22 + 5 = 79) no longer fit; the avatar has to
  drop to 38 px and the tile becomes a contact sheet.
- **80 px**: tile internals need 79 px, 4 rows end at y = 440 — 8 px above the
  body edge and **10 px above the page dots** (450). Nothing collides.

Why the body shrinks from 424 to 392: the last visible row must end *above* the
page dots, otherwise rows are drawn underneath them (that is exactly what makes
today's 4th row look broken). Ending the body at y = 448 gives every row a clean
edge. This also removes the need for the 46 px bottom pad — it existed only to
lift the last row above the dots at the *end* of the scroll, which the shorter
body now does structurally.

Side effect checked: the body is shared with the *selected* state (member card +
`Getränk wählen` + 6 product tiles). At 392 px the product grid still fits
without scrolling (content 362 px of 376 px available) — verified in the same
throwaway build.

### Touch targets

| Measure | Value | Verdict |
|---|---|---|
| Tile | 106 × 80 px = **15.9 × 12.0 mm** | 1.3–1.7 × the 9 mm comfort target |
| Gap between tiles | 8 px = 1.2 mm | dead space, no action |
| Minimum tile height requirement | **80 px = 12 mm** | 44 px (6.6 mm) is *not* enough on this panel |
| Smallest dimension in the design | 80 px (tile height) | ≫ 47 px minimum |

The 1.2 mm gap is deliberately smaller than a finger: since gaps do nothing, a
sloppy tap lands on a neighbour tile, never on a dead zone. That is safe here
because selecting a member is non-destructive — it only opens the member card
(name + that member's balance + `Wechseln`), and a booking needs a second,
deliberate tap on a product.

---

## 4. Decision — scroll affordance

**A persistent 4 px scrollbar on the right edge, and nothing else.**

Rejected alternatives, with what they cost:

| Option | Cost | Why not |
|---|---|---|
| Peek row (part of the next row visible) | ≥ 1 row of height. At 4 columns that is **4 members** = 13 % of a 30-member fund, and it cannot coexist with 4 full rows (4·pitch + peek > 352 px) | Loses the density that justifies 4 columns |
| Bottom fade / gradient | 1 extra object (created once, outside the rebuilt body) | It would dim the 4th row's names, i.e. exactly the content that is hardest to reach |
| `weitere …` text hint | 1 object + new German string | Copy is wrong as soon as the count changes; also competes with the question label |
| Row counter (`1–16 von 30`) | 1 object + 2 strings + rebuild on every scroll | Feels like an admin tool; the kiosk is not a list view |

The scrollbar is right for this kiosk:

- **Zero rebuild cost**: LVGL creates and lays out the scrollbar as part of the
  body it already owns; it is not a child we build.
- **Self-hiding**: LVGL only draws it when the content is taller than the body.
  At 15 members nothing appears; at 30 members a 196 px thumb appears at the
  right edge (y = 63–260), i.e. 53 % of the 376 px content area. The affordance
  is therefore never wrong.
- **Not a touch target**: it is display-only (finger drags produce the same
  scroll), so a 4 px width is fine and cannot be mis-tapped.
- **Position**: x = 470–473, i.e. 6 px right of the last tile column (which
  ends at x = 464), inside the 16 px page padding. Measured in the throwaway
  build.

Style: `LV_SCROLLBAR_MODE_ON` on `book_body_`, `LV_PART_SCROLLBAR` width 4,
radius 2, colour `COLOR_DIM` (`#7A6A62`), opaque. This intentionally overrides
the global `theme::hide_scrollbars`, which stays in place for the other bodies.

Additionally, motion is the affordance on a touch screen: content follows the
finger, and the clipped row at the top of a scrolled list makes the direction
obvious (see [07-page1-30-users-scrolled.png](../ui/screenshots/07-page1-30-users-scrolled.png)).

---

## 5. Decision — no "oft gebucht" block, no badges

When `user_sort = usage` is on, the members with the most bookings this month
are simply the first tiles. There is **no heading, no crown, no frame, no
group** on the first row.

Reasons, in order of weight:

1. **It costs a row.** A 26 px heading plus its 8 px gap = 34 px. The member
   area above the dots is 352 px; 4 rows need 344 px. Adding 34 px pushes the
   fourth row out of the viewport → 12 instead of 16 members visible (−4 slots,
   −13 % of a 30-member fund), to restate something the order already says.
2. **The grouping boundary is arbitrary.** Ties are broken by name
   (`T-013` acceptance), so a group line at, say, "top 4" would split members
   with identical counts — the label would be factually wrong for half of them.
3. **It turns the booking page into a scoreboard.** Page 2 is the ranking. Page
   1 must stay the "who are you?" surface; a `Top-Bucher` frame invites
   comparison and adds colour noise to the calmest page on the device.
4. **It ages badly.** The order is per calendar month, so the "frequent" group
   changes on the 1st of every month; a labelling convention would have to
   change with it.
5. Money stays out of it: page 1 shows one selected member's balance, never
   totals (AGENTS golden rule, `docs/ui.md`).

The plain sorted grid is demonstrated in
[08-page1-30-users-usage-order.png](../ui/screenshots/08-page1-30-users-usage-order.png)
(`--order usage`): the three busiest members (`Hannes` 21, `Mia` 17, `Tilda` 14
bookings) take the first row, while the creation-order leaders drop: `Anna` 12
to row 2, `Ben` 9 to row 3, `Clara` 7 off the first screen entirely. No extra
pixels are needed for that.

---

## 6. Consequences the implementer must handle

1. **The guest tile no longer fits as-is.** Today it stacks avatar 42 + `Gast` +
   `Bar` (`BOOK_GUEST_HINT`, `font_small`) in 100 px with `pad_row` 3, which
   needs 94 px of content at pad 5. At 80 px the hint line is cut mid-glyph
   (measured). Either drop `BOOK_GUEST_HINT` from the tile
   (content 79 px, recommended — the accent border and the label `Gast` already
   distinguish it) or use a 32 px avatar to keep both lines.
2. **The `Gast` tile at the end of a 30-member grid is 336 px away.** A guest sale
   then needs a scroll before any tap. This is a real usability regression
   compared to a 5-member fund. Recommendation (follow-up, not part of the
   geometry change): pin the guest tile outside the scrolling grid — e.g. a
   106 × 80 tile in the header row right of the clock, or a 44 px `Gast` pill.
   It must not participate in `user_sort`.
3. **Body height is per page, not per state.** Set it once in
   `build_page_book` (392 at y = 56). The selected state was checked at that
   height.
4. **Do not animate tiles.** The grid is rebuilt on every booking when
   `user_sort = usage` (the catalog fingerprint contains the order). Opacity or
   translation animations on 30 tiles would run during the rebuild and are the
   fastest way to blow the frame budget while touching the panel.
5. **Keep tiles at exactly 4 LVGL objects** (card, avatar disc, artwork *or*
   initials label, name label). A badge, a shadow style object or a per-tile
   container adds 25 %+ to the object count for no information.

---

## 7. Performance budget

Measured object counts for the whole screen (all three pages) in the simulator:

| Members | Objects | Δ |
|---|---|---|
| 5 (seed) | 168 | – |
| 15 | 208 | +40 → **4 per member** |
| 30 | 268 | +60 → 4 per member |
| 60 | 388 | +120 → 4 per member |

**The geometry change adds zero objects and zero tree depth.** Columns and rows
are produced by one `LV_FLEX_FLOW_ROW_WRAP` container; 30 members are 30 tiles
whether they sit in 3 or 4 columns.

- **LVGL task stack (16 KiB, internal RAM,
  `firmware/components/board/src/board.cpp:1005`) — no increase needed.** Stack
  use is call *depth*, and the member tree is 4 levels deep in both layouts
  (body → grid → tile → avatar → artwork/initials). Object *count* is heap, and
  this port uses `clib` malloc with PSRAM (`CONFIG_LV_USE_CLIB_MALLOC=y`), so the
  extra ~60 tiles at 60 members are PSRAM allocations, not stack.
- **Heap**: ~124 page-1 objects at 30 members, ~250 at 60. Because every rebuild
  allocates the same sizes in the same order, the allocator reuses the same
  blocks; there is no growth trend, only churn.
- **Still to verify on hardware** (not measurable on the host): `status` →
  `lvgl_stack_free` with 30 members after a few bookings. Expect it near today's
  ~6 KiB. If it drops below ~3 KiB, something recursive (e.g. a nested
  per-tile wrapper or a custom draw callback) was introduced — that, not the
  grid, is the cause. Only then is a stack bump to 20 KiB justified.
- Rebuild frequency does not change with the geometry: one rebuild per booking
  when the order changes, deferred through the existing queue.

---

## 8. Degradation at 60 members

| Members | Rows (4 col) | Content | Scroll range | Visible | Scroll depth |
|---|---|---|---|---|---|
| 15 | 4 | 360 px | 0 | all 15 + guest | none |
| 30 | 8 | 712 px | 336 px | 16 (53 %) | 0.9 screens |
| 60 | 16 | 1416 px | 1040 px | 16 (27 %) | 2.8 screens |

At 60 the design still works: the first screen is a stable, readable contact
sheet, the scrollbar shows the position, and with `user_sort = usage` the
members who book daily are on the first screen. What degrades is the *tail*: a
member who joined recently has to swipe up to three times.

Threshold for revisiting: past **~80 members** the scroll range passes four
screens and a member is more likely to be missed than found. At that point the
right change is not more columns but a different structure — a capped
"frequent" grid (2 rows) plus an alphabetical "Alle" view — and it should be its
own task with its own screenshots.

---

## 9. Implementation notes (exact values)

```cpp
// build_page_book()
lv_obj_set_size(book_body_, lv_pct(100), 392);   // was 424
lv_obj_set_pos(book_body_, 0, 56);
lv_obj_set_style_pad_bottom(book_body_, 8, 0);   // was 46
lv_obj_set_scroll_dir(book_body_, LV_DIR_VER);
lv_obj_set_scrollbar_mode(book_body_, LV_SCROLLBAR_MODE_ON);   // replaces hide_scrollbars
lv_obj_set_style_width(book_body_, 4, LV_PART_SCROLLBAR);
lv_obj_set_style_bg_color(book_body_, theme::color(theme::COLOR_DIM), LV_PART_SCROLLBAR);
lv_obj_set_style_bg_opa(book_body_, LV_OPA_COVER, LV_PART_SCROLLBAR);
lv_obj_set_style_radius(book_body_, 2, LV_PART_SCROLLBAR);

// member and guest tiles
lv_obj_set_size(item, 106, 80);                  // was lv_pct(31), 100
lv_obj_set_style_pad_all(item, 5, 0);            // was 8
```

Everything else (avatar 42, `pad_row` 4–5, radius 20, 8 px grid gap, guest accent
border) stays as it is.

---

## 10. Verification

The simulator gained three screenshot flags so these states can be reproduced
without a second pipeline (see `sim/README.md`):

```sh
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --users 15
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --users 30 --scroll 500
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --users 30 --order usage
```

- `--users N` grows the seeded fund to `N` active members with deterministic
  names, avatar presets and per-member booking counts. Default: off, so the five
  original captures stay byte-identical (`make screenshots-check` is green).
- `--scroll PX` scrolls the page-1 list (discovered as the only vertical
  scroller inside the visible tileview page) before the capture.
- `--order usage` returns the members most-bookings-first; it is a stand-in for
  the service order that T-013 adds, with the same tie-break (name ascending),
  so the capture stays valid once the real sort lands.

Committed states (current implementation, reproducible via `make screenshots`):

| File | State |
|---|---|
| [06-page1-15-users.png](../ui/screenshots/06-page1-15-users.png) | 15 members, unscrolled — the "already too many" evidence: 9 of 15 readable, 4th row cut |
| [07-page1-30-users-scrolled.png](../ui/screenshots/07-page1-30-users-scrolled.png) | 30 members, scrolled 500 px — what a scrolled state looks like |
| [08-page1-30-users-usage-order.png](../ui/screenshots/08-page1-30-users-usage-order.png) | 30 members, `usage` order — busiest members first, no grouping |

The *proposed* 4 × 80 geometry was measured and reviewed on a throwaway local
build of `page_book.cpp` (bands at y = 96/184/272/360, 16 readable tiles at 15
and at 30 members, guest-tile hint clipped at 80 px, scrollbar at x = 464–468,
scroll range 336 px at 30 members, product state still fitting at 392 px). Those
pixels are **not committed**, because the committed screenshot set is
regenerated from committed sources — the implementation PR should regenerate
`06`/`07` after applying section 9, and they will then show 16 tiles instead of
9.

---

## 11. Open questions

1. **Guest tile placement** — stay at the end of the grid (today, per
   `T-013` scope) or pin it outside the scrolling area? This record recommends
   pinning (section 6.2) but does not change it, because it changes the
   interaction model, not just the grid.
2. **Name length** — 106 px tiles give 9–10 characters at `font_body`. If real
   names overflow, the cheaper fix is a smaller label (`font_small` 14 px →
   ~12 characters) rather than fewer columns. Not needed for the seeded data.
3. **Portrait/rotated displays** — T-011 will rotate the panel; the column count
   is derived from a fixed 448 px content width and will need the same
   treatment for 90/180/270 if the layout is ever orientation-dependent.

---

## 11. Implemented deviation — the guest action moved to the header

Section 6.1/6.2 recommended dropping the hint and pinning the guest tile. The
implementation does the second part differently from both options sketched
there: the guest action is a **96 × 40 px accent-bordered button in the header
band** (x = 256, y = 8, between the title and the clock) instead of a tile in the
grid.

- It costs **zero** vertical space, so the 4 × 80 geometry from section 3 stays
  exactly as measured (rows at 96 / 184 / 272 / 360, last row ends at 440).
  A question-row button was tried first and cost 8 px: the fourth row then ended
  at 456 and its names were clipped by the page dots.
- It is reachable at any fund size, which was the actual complaint (at 30
  members the trailing tile sits ~340 px below the fold).
- It is created as a child of the page tile and deleted before each rebuild, so
  it is not affected by `lv_obj_clean(book_body_)` and does not leak.
- The simulator click-test now taps (304, 28) instead of the old grid slot.

Verified in the simulator at 30 members: 16 tiles visible, scrollbar present,
guest action always visible; `make test-ui` passes.
