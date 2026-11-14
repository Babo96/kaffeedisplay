# Browser-based preview using Docker

A desktop installation of ESP-IDF, LVGL, SDL2 and Node.js is **not required**.
With Docker Desktop or Docker Engine and Compose installed, from the repository root run:

```sh
docker compose -f compose.preview.yml up --build
```

Then open two **different** local views:

| URL | What is running |
|---|---|
| http://localhost:6080/vnc.html?autoconnect=1 | Actual 480 × 480 LVGL SDL **device simulator** in a browser via noVNC |
| http://localhost:5173 | React **admin web UI** with a separate mock server; admin demo password: `kaffee` |

**Coffee standby demo:** Docker starts the LVGL simulator with
`--standby-demo`. Leave the display untouched for **3 seconds** to see the
fine-line `Kaffeepause` cup. Its three steam strands rise and **alternate
between visible and invisible** as their opacities change in staggered cycles.
Click to dismiss the scene without booking a drink. In this **simulator-only
demonstration**, the screen-off timeout is disabled so an old Docker session
cannot display a frozen, still-lit frame. The production ESP32 still uses
the admin-configured defaults (dim after 60 seconds, backlight off after
600 seconds) and pauses animations when the screen is off.

To view a static frame immediately, see
[the real simulator snapshot](ui/screenshots/06-coffee-standby.png).

Click inside the noVNC view to operate the simulated touchscreen. Keys `1`, `2`, `3` switch pages and `q` quits the simulator. Use the web UI to create and edit test users. Web and LVGL simulator use **separate** fake datasets; changes in the browser's mock admin are *not* mirrored on the SDL display.

Changes to the web UI are reflected via Vite's development server. Changes to the C++ LVGL UI require rebuilding the display container:

```sh
docker compose -f compose.preview.yml up --build display
```

To stop the preview, use Ctrl+C in the Compose terminal or run:

```sh
docker compose -f compose.preview.yml down
```

The VNC server has no password and is bound to **127.0.0.1** only. Never forward port 6080 to an external or public interface.

## Troubleshooting: noVNC shows a black screen

The Docker preview starts a 480 × 480 Xvfb display, waits for X11 to be
ready, then starts x11vnc, websockify and the real LVGL/SDL simulator.
SDL is forced to use its software renderer to work reliably in containers.

After pulling the latest `feature/animated-coffee-standby` branch, rebuild the **image**
(not just restart the old container):

```sh
git pull origin feature/animated-coffee-standby
docker compose -f compose.preview.yml down
docker compose -f compose.preview.yml up --build --force-recreate
```

Open the full address `http://localhost:6080/vnc.html?autoconnect=1`.
If the view is still empty, check:

```sh
docker compose -f compose.preview.yml ps
docker compose -f compose.preview.yml logs --tail=100 display
docker compose -f compose.preview.yml exec display python3 tools/preview/check-display.py
```

The last command should report `PASS` and many sampled colours. The normal
continuous-integration workflow also builds the Docker image, starts the
interactive simulator and checks the actual X11 pixels (not just PNG snapshots).

If the check passes but your browser remains black, reload the noVNC page
and verify it says **Connected**. The VNC port is bound to localhost only.

The **device simulator does not flash real hardware**. Tests of touch electronics, firmware boot, and PSRAM usage still need the ESP32 board.
