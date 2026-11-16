#!/usr/bin/env bash
set -euo pipefail

. "$IDF_PATH/export.sh"
cd /repo/firmware
idf.py build
