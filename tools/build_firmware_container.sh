#!/usr/bin/env bash
set -euo pipefail

apt-get update
apt-get install -y --no-install-recommends libcairo2 python3-venv

standby_tools=/tmp/kaffeekasse-standby-tools
python3 -m venv "$standby_tools"
"$standby_tools/bin/python" -m pip install --disable-pip-version-check \
  --requirement /repo/tools/requirements-standby.txt

. "$IDF_PATH/export.sh"
cd /repo/firmware
STANDBY_PYTHON="$standby_tools/bin/python" idf.py build
