#!/usr/bin/env python3
"""Rebuild the 30 fixed offline avatar illustrations for web and LVGL.

Source: assets/avatar_pack/part*.txt, 20x20 pixels, 16 colours each.
Only the Python standard library is required to regenerate the assets.
"""
import base64
import binascii
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACK_DIR = ROOT / "assets" / "avatar_pack"
W, COUNT, COLORS = 20, 30, 16
SIZE = 38
# The I4 format has no alpha, so each motif is baked onto an opaque plate. The
# plate must be the surface the avatar sits on (theme COLOR_SURFACE, 0x241B18):
# anything darker reads as a hole cut into the card on the 480x480 panel.
PLATE_RGB = (0x24, 0x1B, 0x18)
# LVGL I4 stores 16 palette entries in ARGB8888 (4 bytes each), then two
# 4-bit pixel indices per byte, most-significant nibble first.
STRIDE = (SIZE + 1) // 2
PALETTE_BYTES = COLORS * 4
IMAGE_BYTES = PALETTE_BYTES + SIZE * STRIDE
RECORD_SIZE = 32 + W * W // 2
NAMES = (
    "Fuchs", "Katze", "Hund", "Panda", "Faultier", "Eule",
    "Pinguin", "Waschbaer", "Igel", "Frosch", "Ente", "Kuh",
    "Schwein", "Schaf", "Koala", "Hase", "Biene", "Marienkaefer",
    "Roboter", "Astronaut", "Alien", "Gamepad", "Bier", "Pizza",
    "Kaffee", "Cupcake", "Donut", "Kaktus", "Berge", "Palme",
)


def png(width, height, rgb):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF))
    scan = bytearray()
    for y in range(height):
        scan.append(0)
        scan.extend(rgb[y * width * 3:(y + 1) * width * 3])
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(scan), 9)) + chunk(b"IEND", b""))


def art_from_pack():
    encoded = "".join((PACK_DIR / f"part{i}.txt").read_text().strip() for i in range(1, 5))
    raw = zlib.decompress(base64.b64decode(encoded))
    if len(raw) != RECORD_SIZE * COUNT:
        raise ValueError(f"Avatar pack has {len(raw)} bytes, expected {RECORD_SIZE * COUNT}")
    for index in range(COUNT):
        record = raw[index * RECORD_SIZE:(index + 1) * RECORD_SIZE]
        source_palette = [struct.unpack(">H", record[2 * i:2 * i + 2])[0] for i in range(COLORS)]
        indices = []
        for byte in record[32:]:
            indices.extend((byte >> 4, byte & 15))

        # Preserve the source art's 16-colour palette (no runtime quantizer).
        # In LVGL 9.6 indexed-image palettes must be ARGB8888 in *little-endian*
        # byte order: B, G, R, A. The indexed pixels follow the palette.
        colors = [
            (((c >> 11) & 31) * 255 // 31,
             ((c >> 5) & 63) * 255 // 63,
             (c & 31) * 255 // 31)
            for c in source_palette
        ]
        # Retint the plate entry (the corner pixel's colour) to the card surface
        # so the drawn avatar blends into the circle behind it.
        colors[indices[0]] = PLATE_RGB
        image = bytearray()
        for red, green, blue in colors:
            image.extend((blue, green, red, 255))
        if len(image) != PALETTE_BYTES:
            raise AssertionError("Incorrect I4 palette length")

        # Upscale the original 20px source into the 38px image using
        # bilinear colour interpolation, then map to the nearest of the
        # 16 original palette entries. The embedded web PNG uses exactly
        # these resulting colours, so both UIs show the same artwork.
        rgb = bytearray()
        for y in range(SIZE):
            sy = 2 + (y + 0.5) * 16 / SIZE - 0.5
            y0 = max(0, min(W - 1, int(sy)))
            y1 = min(W - 1, y0 + 1)
            fy = max(0.0, min(1.0, sy - y0))
            row = bytearray()
            for x in range(SIZE):
                sx = 2 + (x + 0.5) * 16 / SIZE - 0.5
                x0 = max(0, min(W - 1, int(sx)))
                x1 = min(W - 1, x0 + 1)
                fx = max(0.0, min(1.0, sx - x0))
                c00 = colors[indices[y0 * W + x0]]
                c10 = colors[indices[y0 * W + x1]]
                c01 = colors[indices[y1 * W + x0]]
                c11 = colors[indices[y1 * W + x1]]
                blended = []
                for channel in range(3):
                    top = c00[channel] * (1 - fx) + c10[channel] * fx
                    bottom = c01[channel] * (1 - fx) + c11[channel] * fx
                    blended.append(round(top * (1 - fy) + bottom * fy))
                idx = min(
                    range(COLORS),
                    key=lambda k: (
                        2 * (blended[0] - colors[k][0]) ** 2
                        + 3 * (blended[1] - colors[k][1]) ** 2
                        + (blended[2] - colors[k][2]) ** 2
                    ),
                )
                row.append(idx)
                rgb.extend(colors[idx])
            for x in range(0, SIZE, 2):
                image.append((row[x] << 4) | (row[x + 1] if x + 1 < SIZE else 0))
        if len(image) != IMAGE_BYTES:
            raise AssertionError(f"I4 data length {len(image)}, expected {IMAGE_BYTES}")
        yield index + 1, bytes(image), png(SIZE, SIZE, rgb)

def main():
    header = [
        "// Generated by tools/generate_avatar_art.py; edit assets/avatar_pack, not this file.",
        "// clang-format off",
        "#pragma once",
        '#include "lvgl.h"',
        "namespace kaffee::ui {",
    ]
    ts = [
        "// Generated by tools/generate_avatar_art.py. Offline fixed avatar artwork.",
        "export const AVATAR_ART = [",
    ]
    for index, i4_data, png_bytes in art_from_pack():
        payload = ",".join(f"0x{byte:02x}" for byte in i4_data)
        header.append(
            f"LV_ATTRIBUTE_MEM_ALIGN static const uint8_t avatar_art_{index}_data[] = {{{payload}}};"
        )
        header.append(
            f"static const lv_image_dsc_t avatar_art_{index} = {{"
            f"{{ LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_I4, 0, {SIZE}, {SIZE}, {STRIDE}, 0 }}, "
            f"sizeof(avatar_art_{index}_data), avatar_art_{index}_data, nullptr }};"
        )
        ts.append(
            f'  "data:image/png;base64,{base64.b64encode(png_bytes).decode()}", // {index} {NAMES[index-1]}'
        )
    header.append("inline const lv_image_dsc_t* avatar_art(int id) {")
    header.append("    switch (id) {")
    for index in range(1, COUNT + 1):
        header.append(f"      case {index}: return &avatar_art_{index};")
    header += ["      default: return nullptr;", "    }", "}", "}  // namespace kaffee::ui", "// clang-format on"]
    ts.append("] as const;")
    import json
    ts.append("export const AVATAR_NAMES = " + json.dumps(list(NAMES), ensure_ascii=False) + " as const;")
    out_cpp = ROOT / "firmware/components/ui/src/avatar_art_generated.hpp"
    out_ts = ROOT / "web/src/components/avatarArt.generated.ts"
    out_cpp.parent.mkdir(parents=True, exist_ok=True)
    out_ts.parent.mkdir(parents=True, exist_ok=True)
    out_cpp.write_text("\n".join(header) + "\n")
    out_ts.write_text("\n".join(ts) + "\n")
    print("Generated", out_cpp, out_cpp.stat().st_size, "bytes")
    print(f"Indexed I4 payload: {IMAGE_BYTES} bytes/avatar, {COUNT * IMAGE_BYTES} bytes total")
    print("Generated", out_ts, out_ts.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
