#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "kaffee/domain.hpp"
#include "kaffee/service.hpp"

namespace sim {

// Deterministic fake backend for the host simulator. It wraps the real domain
// Ledger so balances, leaderboards and voids follow production code.
class FakeBackend final : public kaffee::IUiBackend {
  public:
    FakeBackend();

    // Follow the IUiBackend snapshot accessor return type (ADR 0012 changes it
    // from const-ref to by-value). This keeps the simulator compiling before
    // and after T-001 lands.
    using UsersReturn = decltype(std::declval<const kaffee::IUiBackend&>().users());
    using ProductsReturn = decltype(std::declval<const kaffee::IUiBackend&>().products());

    UsersReturn users() const override;
    ProductsReturn products() const override;
    std::optional<kaffee::User> find_user(const std::string& id) const override;
    std::optional<kaffee::Product> find_product(const std::string& id) const override;
    kaffee::Cents balance(const std::string& user_id) const override;
    kaffee::Result<kaffee::Event> purchase(const std::string& user_id,
                                           const std::string& product_id,
                                           const std::string& request_id) override;
    kaffee::Result<kaffee::Event> guest_sale(const std::string& product_id,
                                             const std::string& request_id) override;
    kaffee::Leaderboard leaderboard(kaffee::Timestamp from, kaffee::Timestamp to) const override;
    kaffee::SystemStats stats() const override;
    kaffee::TimeInfo time_info() const override;
    kaffee::NetworkStatus network_status() const override;
    kaffee::DisplaySettings display_settings() const override;
    kaffee::Result<kaffee::Unit> apply_display_settings(
        const kaffee::DisplaySettings& settings) override;
    kaffee::Result<kaffee::Unit> start_ap() override;
    kaffee::Result<kaffee::Unit> stop_ap() override;
    kaffee::Result<kaffee::Unit> extend_ap(int seconds) override;
    kaffee::AppSettings app_settings() const override;
    bool must_change_admin_password() const override;
    std::string setup_password() const override;
    kaffee::Result<kaffee::Event> undo_booking(const std::string& event_id,
                                               const std::string& request_id) override;
    void set_change_callback(kaffee::ChangeCallback callback) override;
    void refresh() override;

    // Simulator controls.
    //
    // Screenshot helpers for the T-013 layout study. `set_member_count` grows the
    // seeded fund to `total` active members with a deterministic spread of
    // bookings; `set_usage_order` returns the members in consumption order
    // (most bookings this month first, ties by name), the order the service will
    // provide once T-013 lands. Both are opt-in so the default frames stay
    // byte-identical.
    void set_member_count(int total);
    void set_usage_order(bool enabled);
    void set_rotation(int degrees);
    void set_brightness(int percent);
    void set_time_trusted(bool trusted);
    void set_guest_enabled(bool enabled);
    int count_events(kaffee::EventType type) const;
    int rotation() const {
        return display_.rotation;
    }
    int brightness() const {
        return display_.brightness_active;
    }
    kaffee::Timestamp now() const {
        return now_;
    }

  private:
    void seed();
    void notify();
    std::string next_request(const char* prefix);

    kaffee::Ledger ledger_;
    kaffee::Timestamp now_ = 1'794'386'460;  // 2026-11-11 09:41 Europe/Berlin
    kaffee::DisplaySettings display_;
    kaffee::ChangeCallback change_callback_;

    bool ap_active_ = false;
    bool ap_starting_ = false;
    int ap_remaining_s_ = 900;
    std::string ap_ssid_ = "Kaffeekasse-4848";
    std::string ap_password_ = "espresso24";
    std::string ap_ip_ = "192.168.4.1";
    bool sta_connected_ = true;
    std::string sta_ip_ = "192.168.1.42";
    int sta_rssi_ = -58;

    bool time_trusted_ = true;
    bool guest_enabled_ = true;
    bool usage_order_ = false;
    int request_counter_ = 0;
};

}  // namespace sim
