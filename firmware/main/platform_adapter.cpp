#include "platform_adapter.hpp"

#include <cstdio>
#include <utility>

#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "kaffee/sd_backup.hpp"

namespace kaffee {
namespace {

std::string mac_string() {
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK &&
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK &&
        esp_read_mac(mac, ESP_MAC_BT) != ESP_OK) {
        return {};
    }
    char text[18] = {};
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    return text;
}

}  // namespace

PlatformAdapter::PlatformAdapter(EspDisplay& display, NetworkManager& network,
                                 sd::SdStatusMonitor& sd_status)
    : display_(display), network_(network), sd_status_(sd_status) {}

SystemStats PlatformAdapter::stats() const {
    const BoardStats board = display_.stats();
    SystemStats stats;
    stats.temperature_valid = board.temperature_valid;
    stats.chip_temperature_c = board.chip_temperature_c;
    stats.uptime_s = board.uptime_s;
    stats.free_heap = board.free_heap;
    stats.min_free_heap = board.min_free_heap;
    stats.free_psram = board.free_psram;
    stats.network = network_.status();
    stats.time = network_.time_info();
    stats.mac = mac_string();
    const sd::SdStatus sd = sd_status_.snapshot();
    stats.sd_card_present = sd.card_present;
    stats.sd_backup_ok = sd.backup_ok;
    stats.sd_last_success = sd.last_success;
    stats.sd_last_error = sd.last_error;
    if (const esp_app_desc_t* app = esp_app_get_description(); app != nullptr) {
        stats.firmware_version = app->version;
        stats.build_date = app->date;
    }
    return stats;
}

TimeInfo PlatformAdapter::time_info() const {
    return network_.time_info();
}

uint64_t PlatformAdapter::random_u64() {
    // Two independent hardware RNG words. The Ledger IdFn and session tokens
    // draw from here; never use the host-only default_id() on the device.
    return (static_cast<uint64_t>(esp_random()) << 32) | static_cast<uint64_t>(esp_random());
}

Result<Unit> PlatformAdapter::sync_time(Timestamp epoch_seconds, const std::string& source) {
    return network_.sync_time(epoch_seconds, source);
}

NetworkStatus PlatformAdapter::network_status() const {
    return network_.status();
}

Result<Unit> PlatformAdapter::start_ap() {
    return network_.start_ap();
}

Result<Unit> PlatformAdapter::stop_ap() {
    return network_.stop_ap();
}

Result<Unit> PlatformAdapter::extend_ap(int seconds) {
    return network_.extend_ap(seconds);
}

DisplaySettings PlatformAdapter::display_settings() const {
    return display_.settings();
}

Result<Unit> PlatformAdapter::apply_display_settings(const DisplaySettings& settings) {
    return display_.apply_settings(settings);
}

Result<Unit> PlatformAdapter::show_display_test() {
    return display_.show_test_pattern();
}

Result<Unit> PlatformAdapter::apply_network_settings(const NetworkSettings& settings) {
    return network_.apply_settings(settings);
}

Result<Unit> PlatformAdapter::apply_time_settings(const TimeSettings& settings) {
    return network_.apply_time_settings(settings);
}

Result<Unit> PlatformAdapter::reset_network_credentials() {
    return network_.factory_reset_credentials();
}

void PlatformAdapter::notify_ledger_changed() {
    // KaffeeService already invokes the queued UI change callback on its
    // notifier thread; nothing extra is required here.
}

void PlatformAdapter::set_ap_stopped_callback(std::function<void()> callback) {
    ap_stopped_callback_ = std::move(callback);
}

void PlatformAdapter::set_ap_active(bool active) {
    if (ap_active_.exchange(active) == active) return;
    display_.set_stay_awake(active);
}

void PlatformAdapter::notify_ap_stopped() {
    set_ap_active(false);
    if (ap_stopped_callback_) ap_stopped_callback_();
}

}  // namespace kaffee
