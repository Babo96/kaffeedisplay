#!/usr/bin/env python3
"""Compare two directories of 480x480 screenshots pixel-by-pixel.

The simulator writes deterministic PNGs with filter type 0. This script only
uses the Python standard library, so CI does not need Pillow or ImageMagick.
"""
from __future__ import annotations

import hashlib
import struct
import sys
import zlib
from pathlib import Path


def read_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(f"{path}: not a PNG")

    pos = 8
    width = height = 0
    bit_depth = color_type = 0
    idat = bytearray()
    while pos + 12 <= len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        payload = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", payload[:10])
        elif kind == b"IDAT":
            idat.extend(payload)
        elif kind == b"IEND":
            break

    if bit_depth != 8 or color_type != 2:
        raise ValueError(f"{path}: expected 8-bit RGB")
    raw = zlib.decompress(bytes(idat))
    stride = width * 3 + 1
    if len(raw) != stride * height:
        raise ValueError(f"{path}: unexpected raw size")
    for row in range(height):
        if raw[row * stride] != 0:
            raise ValueError(f"{path}: unsupported PNG filter")
    return width, height, raw


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_screenshots.py <expected-dir> <generated-dir>")
        return 2
    expected_dir = Path(sys.argv[1])
    generated_dir = Path(sys.argv[2])
    failed = False
    files = sorted(expected_dir.glob("*.png"))
    if not files:
        print(f"no screenshots in {expected_dir}")
        return 1

    for expected_path in files:
        generated_path = generated_dir / expected_path.name
        try:
            ew, eh, eraw = read_png(expected_path)
            gw, gh, graw = read_png(generated_path)
        except (OSError, ValueError) as exc:
            print(f"FAIL {expected_path.name}: {exc}")
            failed = True
            continue
        if (ew, eh) != (gw, gh):
            print(f"FAIL {expected_path.name}: size {gw}x{gh} != {ew}x{eh}")
            failed = True
            continue
        if eraw != graw:
            diff = sum(1 for a, b in zip(eraw, graw) if a != b)
            print(f"FAIL {expected_path.name}: {diff} byte(s) differ")
            failed = True
            continue
        digest = hashlib.sha256(eraw).hexdigest()
        print(f"ok   {expected_path.name}: {digest}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
