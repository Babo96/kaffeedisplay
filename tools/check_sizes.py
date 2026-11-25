#!/usr/bin/env python3
"""Check firmware and admin bundle size budgets."""
from __future__ import annotations

import gzip
import json
import sys
from pathlib import Path

MAX_WEB_GZIP = 400 * 1024
MAX_FIRMWARE = 3 * 1024 * 1024
# `partitions.csv`: storage starts at 0x620000. A merged factory image is
# flashed at 0x0, so it must end before this offset to leave the ledger alone.
STORAGE_OFFSET = 0x620000

failed = False


def check(label: str, size: int, limit: int) -> None:
    global failed
    ok = size <= limit
    print(f"{label}: {size / 1024:.1f} KiB / {limit / 1024:.0f} KiB {'ok' if ok else 'TOO BIG'}")
    failed = failed or not ok


dist = Path("web/dist")
if dist.exists():
    total = 0
    for path in dist.rglob("*"):
        if path.is_file():
            total += len(gzip.compress(path.read_bytes(), 9))
    check("web bundle (gzip, all assets)", total, MAX_WEB_GZIP)
else:
    print("web bundle: not built, skipped")

embedded = Path("firmware/components/webassets/generated/manifest.json")
if embedded.exists():
    payload = json.loads(embedded.read_text(encoding="utf-8"))
    check("embedded web assets (gzip)", int(payload["gzip_bytes"]), MAX_WEB_GZIP)
    check("embedded web assets (raw)", int(payload["original_bytes"]), 4 * MAX_WEB_GZIP)
else:
    print("embedded web assets: not generated, skipped")

bins = sorted(Path("firmware/build").glob("*.bin")) if Path("firmware/build").exists() else []
if bins:
    for binary in bins:
        if binary.name == "merged-binary.bin":
            # A factory image bundles bootloader, partition table, OTA data and
            # app, so the app budget does not apply. It is flashed at 0x0 and
            # must stay clear of the storage partition, or flashing a release
            # would cover the ledger.
            check("merged-binary.bin (before storage 0x620000)", binary.stat().st_size, STORAGE_OFFSET)
        else:
            check(binary.name, binary.stat().st_size, MAX_FIRMWARE)
else:
    print("firmware: not built, skipped")

raise SystemExit(1 if failed else 0)
