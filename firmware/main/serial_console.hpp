#pragma once

#include "kaffee/service.hpp"

namespace kaffee {

class EspDisplay;

namespace ui {
class UiControl;
}

// Line-based serial console on UART0 (CH340, 115200 8N1). It is a development,
// recovery and HIL interface; runtime operation never depends on it. Normal
// commands go through KaffeeService/IPlatform. Diagnostic `display test` asks
// the board to draw, `ui tap` injects into the real LVGL indev, and `ui book`
// queues an app callback on the LVGL task; all three are write-gated like the
// other HIL commands.
//
// Protocol and commands are documented in docs/serial-cli.md.
bool start_serial_console(KaffeeService& service, IPlatform& platform, EspDisplay& display,
                          ui::UiControl* ui_control);
void set_serial_console_ui(ui::UiControl* ui_control);

}  // namespace kaffee
