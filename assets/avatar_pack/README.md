# Avatar source pack

The 30 built-in user avatars are authored as one compact source pack.
`part1.txt` … `part4.txt` carry the same record stream split into
2000-character chunks — that split is an artifact of how the pack was produced
and moved, not a design decision. Concatenate the parts to recover the stream.

## Format

Each record is 232 bytes:

| Bytes | Content |
|---|---|
| 0–31 | 16 palette entries as RGB565 values (2 bytes each, little-endian) |
| 32–231 | 20 × 20 palette indices, 4 bits each, high nibble first |

## Regenerating the shipped artwork

```sh
python3 tools/generate_avatar_art.py   # rewrite the generated art
python3 tools/check_avatar_i4.py       # verify record layout and palettes
make check                             # also runs the verifier via `assets`
```

The generator trims the 2 px border of each 20 × 20 record, bilinearly upscales
the inner 16 × 16 field to 38 × 38 and snaps every sample back to one of the 16
source colours — so the device and the admin never diverge on hue. It writes:

- `firmware/components/ui/src/avatar_art_generated.hpp` — LVGL I4 indexed images
  with an ARGB8888 palette, drawn on the 480 × 480 panel;
- `web/src/components/avatarArt.generated.ts` — the same pixels as base64 PNGs
  for the admin SPA.

Both files are generated and checked in; edit the pack, never the generated
files. CI regenerates them and fails on any diff, so a hand-edited generated
file cannot land.

## Provenance

The pack was produced for this project together with the avatar feature
(PR #12): the motifs are drawn as abstract shapes (animals, technology, coffee
and objects) rather than adopted from a third-party icon set, and the colours
are chosen to sit inside the existing device theme palette. No preset encodes a
rank, balance or account state — the avatar is decorative, and the initials
badge on the device still identifies the user. The pack is covered by the
repository licence.
