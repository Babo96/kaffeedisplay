# Tools

Host-side helpers. They are not part of the firmware runtime.

| Tool | Purpose |
|---|---|
| `bootstrap.sh` | Verify host tools and install web dependencies. |
| `embed_web.py` | Deterministic gzip/raw embedding of `web/dist` into `firmware/components/webassets/generated/`. `--check` verifies freshness. |
| `hil.py` | pytest/pyserial/requests hardware-in-the-loop smoke test. Skips cleanly when `/dev/ttyUSB0` is absent. |
| `settime.py` | Set or read the device clock over the serial CLI. |
| `serial_proto.py` | Line-based serial CLI client shared by `hil.py` and `settime.py`. |
| `check_sizes.py` | Web gzip, embedded asset and firmware size budgets. |
| `check_docs.py` | Relative Markdown link checker (no network). |
| `lint_openapi.py` | Offline OpenAPI sanity check; skips deep validation if PyYAML is absent. |
| `requirements-tools.txt` | Pinned Python dependencies for HIL and linting (`make tools-deps`). |
| `requirements-standby.txt` | Pinned build-only SVG/image dependencies for the LVGL standby cup. |

## Common commands

```sh
make assets      # python3 tools/embed_web.py
make tools-deps  # pip install -r tools/requirements-tools.txt
make standby-tools  # create the pinned standby image tool environment
make standby-cup-check  # test reproducible build-local LVGL image generation
make hil         # python3 tools/hil.py --port /dev/ttyUSB0
python3 tools/settime.py --status
python3 tools/embed_web.py --check
python3 tools/check_docs.py
```

`make build`, `make check` and CI generate the standby image inside the build
directory. Firmware builds in Docker install its pinned Python packages there;
native builds also require the system Cairo shared library. Generated output is
gitignored; see `firmware/components/webassets/README.md` for the firmware
asset interface.
