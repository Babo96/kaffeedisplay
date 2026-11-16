#!/bin/sh
set -eu

# Match the real device's 480x480 viewport. With no window manager, a larger
# virtual desktop may look entirely black while the SDL window is off-screen.
export DISPLAY=:99
export SDL_VIDEODRIVER=x11
export SDL_RENDER_DRIVER=software
export SDL_AUDIODRIVER=dummy
export LIBGL_ALWAYS_SOFTWARE=1

echo "[preview] Starting Xvfb (480x480)..."
Xvfb :99 -screen 0 480x480x24 -ac -nolisten tcp &
xvfb_pid=$!

# Xvfb starts asynchronously: SDL and x11vnc must not race its X socket.
ready=0
for attempt in $(seq 1 100); do
    if xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
        ready=1
        break
    fi
    if ! kill -0 "$xvfb_pid" 2>/dev/null; then
        echo "[preview] Xvfb exited before becoming ready" >&2
        exit 1
    fi
    sleep 0.1
done
if [ "$ready" -ne 1 ]; then
    echo "[preview] No X11 display after 10 seconds" >&2
    exit 1
fi
echo "[preview] X11 display ready."

x11vnc -display "$DISPLAY" -forever -nopw -shared -rfbport 5900 -quiet -noxdamage &
vnc_pid=$!
websockify --web /usr/share/novnc 6080 127.0.0.1:5900 &
web_pid=$!

# Fail if VNC or websockify cannot start; don't serve a misleading black view.
sleep 0.4
for pid in "$vnc_pid" "$web_pid"; do
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "[preview] VNC/websockify failed; inspect docker compose logs display" >&2
        exit 1
    fi
done

echo "[preview] Launching LVGL simulator with SDL software rendering."
echo "[preview] Open http://localhost:6080/vnc.html?autoconnect=1"
# Show the moving steam promptly in Docker; do not simulate a black
# hardware backlight after ten minutes. This flag only affects fake data.
exec ./sim/build/kaffeekasse-sim --standby-demo
