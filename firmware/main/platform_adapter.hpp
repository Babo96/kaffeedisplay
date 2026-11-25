#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "kaffee/board_esp.hpp"
#include "kaffee/net_manager.hpp"
#include "kaffee/service.hpp"

namespace kaffee {

namespace sd {
class SdStatusMonitor;
}

// Firmware IPlatform implementation. It joins the board display and the network
// manager into the single platform facade the service and API expect.
//
// The board owns LVGL; apply_display_settings()/show_display_test() are safe
// from the HTTP task because EspDisplay serialises them through esp_lvgl_port.
class PlatformAdapter final : public IPlatform {
  public:
    PlatformAdapter(EspDisplay& display, NetworkManager& network, sd::SdStatusMonitor& sd_status);

    SystemStats stats() const override;
    TimeInfo time_info() const override;
    uint64_t random_u64() override;
    Result<Unit> sync_time(Timestamp epoch_seconds, const std::string& source) override;

    NetworkStatus network_status() const override;
    Result<Unit> start_ap() override;
    Result<Unit> stop_ap() override;
    Result<Unit> extend_ap(int seconds) override;

    DisplaySettings display_settings() const override;
    Result<Unit> apply_display_settings(const DisplaySettings& settings) override;
    Result<Unit> show_display_test() override;

    Result<Unit> apply_network_settings(const NetworkSettings& settings) override;
    Result<Unit> apply_time_settings(const TimeSettings& settings) override;
    Result<Unit> reset_network_credentials() override;

    void notify_ledger_changed() override;
    void set_ap_stopped_callback(std::function<void()> callback) override;

    // Called from the network listener when the confirmed AP state changes.
    // Keeps the panel awake for the QR/provisioning screen.
    void set_ap_active(bool active);

    // Invoked by the network listener on a confirmed AP stop. Runs the service
    // callback (session clearing) without blocking the Wi-Fi timer task.
    void notify_ap_stopped();

  private:
    EspDisplay& display_;
    NetworkManager& network_;
    sd::SdStatusMonitor& sd_status_;
    std::function<void()> ap_stopped_callback_;
    std::atomic<bool> ap_active_{false};
};

}  // namespace kaffee
