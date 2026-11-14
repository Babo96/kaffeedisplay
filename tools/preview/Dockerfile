# Browser-accessible LVGL simulator. No host SDK, SDL, or GUI installation.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git cmake make g++ libsdl2-dev zlib1g-dev \
    libcairo2 python3 python3-venv xvfb x11vnc novnc websockify x11-utils x11-apps xdotool \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY . .
RUN make sim-build
RUN chmod +x tools/preview/run-simulator.sh

EXPOSE 6080
ENTRYPOINT ["tools/preview/run-simulator.sh"]
