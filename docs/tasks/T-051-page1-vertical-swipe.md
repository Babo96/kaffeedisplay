# T-051 — Page 1: vertical swipe instead of reaching for the arrows

Status: in-review
Track: ui
Owner: designer lane (3-lane parallel run, lane 2 of 3)
Branch: `feat/51-page1-vertical-swipe`
Issue: [#51 — nach unten swipen Seite 1](https://github.com/Babo96/kaffeedisplay/issues/51)

> "Auf der ersten seite soll auch die möglichkeit bestehen nach unten/oben zu
> swipen statt die buttons zu nutzen"

## Scope

Page 1 (`Buchen`) can be paged vertically by dragging, as an alternative to the
`▲`/`▼` arrows in the right margin. Pages 2/3, the domain/service/API contracts
and the German UI text are untouched.

## What the gesture does

One recogniser in `ui_app.cpp` reads the raw touch stream every 10 ms - the same
one that flips pages horizontally (PRs #42–#46). It now resolves two axes:

| Rule | Value | Why |
|---|---|---|
| Dead zone / axis lock | **24 px** of total excursion | Below it nothing is decided and the press point stays the reference, so a slow finger accumulates instead of being re-anchored sample by sample |
| Axis decision | larger of `abs(dx)`, `abs(dy)` in the first sample past the dead zone; **horizontal on a tie** | A diagonal drag resolves to exactly one action, deterministically. The axis is then frozen for the rest of the gesture |
| Horizontal action | flip one page at **≥70 px** (unchanged) | PRs #43–#46 |
| Vertical action | step the member grid at **≥100 px**, up = next page, down = previous page | Same order as the arrows and as the horizontal flip |
| Steps per gesture | **one**; a drag that travels back through the press point re-arms (both axes within 24 px) and can step the other way | Paged grid: one page per deliberate swipe, no skipping a page because the finger was fast |
| Click suppression | set as soon as either action fires and kept until the finger is up - also across a re-arm | A drag must never book a drink, not even a drag that returns to its start |

Why snapping and not "following the finger": the geometry is paged (3 × 4 tiles
of 124 × 80 px, 12 members per page, see
[T-013](T-013-page1-layout.md) section 12). A drag that follows the finger
redraws the whole viewport per pointer sample, which this panel cannot sustain
(~25 fps measured, newly exposed rows "popped in"). A page step is one rebuild,
deferred through the existing change queue, exactly like an arrow press. The
vertical threshold is 100 px ≈ one tile row (88 px) so a normal swipe clears it
once and a very long drag still moves only one page.

## Affordance

Unchanged, deliberately: the `▲`/`▼` arrows and the `n/N` counter in the right
margin are the "the list continues" signal, now literally the same action as the
gesture. T-013 section 4 rejected a bottom fade (it dims the 4th row's names,
i.e. the content that is hardest to reach) and any `weitere …` text (copy ages
badly, competes with the question label). No help or connection text was added
(AGENTS golden rule 7 / "never do").

## Safety and robustness

- **No double fire**: product, guest, member and `Wechseln` clicks all consult
  `swallow_drag_click()`; a paged drag's release is swallowed. Additionally
  LVGL's own delete handling (`lv_indev_wait_release`) discards the release when
  the rebuild removed the pressed tile.
- **Standby / test pattern**: the recogniser returns before it looks at the
  stream while either overlay is up, so a wake swipe cannot page the grid
  underneath (unchanged behaviour from #44).
- **Slow finger**: the press point is fixed inside the dead zone (no per-sample
  re-anchoring), so a 6 px-per-sample drag still reaches the threshold.
- **Short drag**: below 100 px nothing pages and the release stays a normal tap.
- **Reversed drag**: returning through the press point re-arms and steps back.
- **Panel edge**: leaving the panel ends the press; the gesture state is dropped
  on the next release sample. Coordinates are clamped by the display.
- **Pages 2/3**: a vertical drag there is ignored by the UI (their lists keep
  their own scrolling) and never suppresses a button click.
- **Selected state**: the member grid is not built, so nothing pages; the drag
  only suppresses the accidental booking.

## Evidence

`make test-ui` (LVGL click/hit test in the SDL simulator) now covers, on a
30-member fund:

- swipe up opens page 2, again page 3, and clamps at the last page;
- swipe down steps back and clamps at the first page;
- `purchases` is identical before and after the whole block (paging never books);
- a 60 px drag does not page and still behaves as a tap;
- a slow 6 px/sample drag pages;
- up-then-down through the press point ends where it started;
- a 45° diagonal flips the page and leaves the member page alone;
- the pre-existing swipe/dot/standby/AP checks stay green.

The standby "steam actually moves" check was sampled at two fixed frames and
landed in the 52-frame hold phase of the 9.6 s cycle once the fund grew to 30
members. It now samples the whole cycle (min/max of the strand position), which
is what the neighbouring opacity check already did.

No screenshot changed: the visible pixels of page 1 (and of every other page)
are identical for equal data, because paging only changes *which* 12 tiles are
built. `make screenshots-check` is green.

Not verified without hardware: the feel of the 100 px threshold under a real
finger, and the frame timing of the deferred rebuild while a finger is still
down (the simulator cannot reproduce RGB panel transfer cost).

## Conflicts

`page_book.cpp` is also edited by the guest-price lane (T-049 price display). The
hunks here are `step_book_page()`, the `book_page_count_` bookkeeping and the
`swallow_drag_click()` guards in `user_click_cb`/`change_user_cb`; they do not
overlap T-049's price-label hunk, so a textual merge should be clean. The stats
lane (T-048) does not touch page 1.
