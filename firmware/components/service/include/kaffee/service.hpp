#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kaffee/domain.hpp"
#include "kaffee/settings.hpp"
#include "kaffee/storage.hpp"

namespace kaffee {

struct NetworkStatus {
    bool ap_active = false;
    bool ap_starting = false;
    int ap_remaining_s = 0;
    std::string ap_ssid;
    std::string ap_password;
    std::string ap_ip;
    bool sta_connected = false;
    std::string sta_ip;
    int sta_rssi = 0;
};

struct TimeInfo {
    Timestamp now = 0;
    TimeSource source = TimeSource::Unset;
    Timestamp last_sync = 0;
    std::string last_source;  // "browser", "sntp", "manual", "restored"
    bool trusted = false;
};

struct SystemStats {
    bool temperature_valid = false;
    float chip_temperature_c = 0.0f;
    uint64_t uptime_s = 0;
    uint32_t free_heap = 0;
    uint32_t min_free_heap = 0;
    uint32_t free_psram = 0;
    StorageInfo storage;
    std::string firmware_version;
    std::string build_date;
    std::string mac;
    NetworkStatus network;
    TimeInfo time;
    Timestamp last_backup_at = 0;
    int drinks_today = 0;
};

// Hardware/network services the application needs. Implemented by the board and
// net components; the simulator provides a fake.
class IPlatform {
  public:
    virtual ~IPlatform() = default;
    virtual SystemStats stats() const = 0;
    virtual TimeInfo time_info() const = 0;
    virtual uint64_t random_u64() = 0;
    virtual Result<Unit> sync_time(Timestamp epoch_seconds, const std::string& source) = 0;

    virtual NetworkStatus network_status() const = 0;
    virtual Result<Unit> start_ap() = 0;
    virtual Result<Unit> stop_ap() = 0;
    virtual Result<Unit> extend_ap(int seconds) = 0;

    virtual DisplaySettings display_settings() const = 0;
    virtual Result<Unit> apply_display_settings(const DisplaySettings& settings) = 0;
    virtual Result<Unit> show_display_test() = 0;

    // Settings are owned by the service. These hooks let the service apply the
    // stored network/time settings without exposing read-only secrets through
    // Settings::operator; device platforms override them. Defaults are no-ops
    // so host fakes keep compiling. See ADR 0013.
    virtual Result<Unit> apply_network_settings(const NetworkSettings& settings) {
        (void)settings;
        return Result<Unit>::ok(Unit{});
    }
    virtual Result<Unit> apply_time_settings(const TimeSettings& settings) {
        (void)settings;
        return Result<Unit>::ok(Unit{});
    }

    // Erase persistent network credentials (AP password, STA credentials and
    // the restored wall clock) as part of a factory reset. Device platforms
    // override this; the default reports that the feature is unavailable.
    virtual Result<Unit> reset_network_credentials() {
        return Result<Unit>::fail("unsupported", "network credential reset is not supported");
    }

    // Called after a successful ledger mutation so the UI can refresh.
    virtual void notify_ledger_changed() = 0;

    // Optional listener for AP shutdowns that happen outside the service (for
    // example the AP auto-off timer). The service uses it to drop admin
    // sessions. Device platforms should override; the default is a no-op so
    // existing fakes keep working.
    virtual void set_ap_stopped_callback(std::function<void()> callback) {
        (void)callback;
    }
};

struct Session {
    std::string token;
    std::string csrf_token;
    Timestamp created_at = 0;
    Timestamp expires_at = 0;
};

using ChangeCallback = std::function<void()>;

// Narrow interface consumed by the LVGL UI. The simulator provides a fake.
class IUiBackend {
  public:
    virtual ~IUiBackend() = default;
    // Snapshot accessors return copies. Returning references after releasing
    // the service mutex would race with mutations; see ADR 0012.
    virtual std::vector<User> users() const = 0;
    virtual std::vector<Product> products() const = 0;
    virtual std::optional<User> find_user(const std::string& id) const = 0;
    virtual std::optional<Product> find_product(const std::string& id) const = 0;
    virtual Cents balance(const std::string& user_id) const = 0;
    virtual Result<Event> purchase(const std::string& user_id, const std::string& product_id,
                                   const std::string& request_id) = 0;
    virtual Result<Event> guest_sale(const std::string& product_id,
                                     const std::string& request_id) = 0;
    virtual Leaderboard leaderboard(Timestamp from, Timestamp to) const = 0;
    virtual SystemStats stats() const = 0;
    virtual TimeInfo time_info() const = 0;
    virtual NetworkStatus network_status() const = 0;
    virtual DisplaySettings display_settings() const = 0;
    virtual Result<Unit> apply_display_settings(const DisplaySettings& settings) = 0;
    virtual Result<Unit> start_ap() = 0;
    virtual Result<Unit> stop_ap() = 0;
    virtual Result<Unit> extend_ap(int seconds) = 0;
    virtual AppSettings app_settings() const = 0;
    // First-run admin state. While must_change is true the device shows the
    // persisted setup password so the admin can log in and change it.
    virtual bool must_change_admin_password() const = 0;
    virtual std::string setup_password() const = 0;
    // Device-side undo of the most recent member booking (actor "ui").
    virtual Result<Event> undo_booking(const std::string& event_id,
                                       const std::string& request_id) = 0;
    virtual void set_change_callback(ChangeCallback callback) = 0;
    virtual void refresh() = 0;
};

class KaffeeService : public IUiBackend {
  public:
    static Result<std::unique_ptr<KaffeeService>> create(IEventStore& store,
                                                         ISettingsStore& settings_store,
                                                         IPlatform& platform);
    virtual ~KaffeeService() = default;

    // --- UI-facing ---
    // The vector accessors return copies, not references; see ADR 0012.
    virtual std::vector<User> users() const = 0;
    virtual std::vector<Product> products() const = 0;
    virtual std::optional<User> find_user(const std::string& id) const = 0;
    virtual std::optional<Product> find_product(const std::string& id) const = 0;
    virtual Cents balance(const std::string& user_id) const = 0;
    virtual Summary summary() const = 0;
    virtual Leaderboard leaderboard(Timestamp from, Timestamp to) const = 0;
    virtual SystemStats stats() const = 0;
    virtual TimeInfo time_info() const = 0;
    virtual NetworkStatus network_status() const = 0;
    virtual DisplaySettings display_settings() const = 0;
    virtual Result<Unit> apply_display_settings(const DisplaySettings& settings) = 0;

    virtual Result<Event> purchase(const std::string& user_id, const std::string& product_id,
                                   const std::string& request_id) = 0;
    virtual Result<Event> guest_sale(const std::string& product_id,
                                     const std::string& request_id) = 0;

    // --- Admin operations ---
    // avatar_id: -1 = not specified, 0 = initials only, 1..kMaxUserAvatarId = a
    // preset. For create_user "not specified" picks a random preset; for
    // update_user it keeps the current selection. Out-of-range values fail.
    virtual Result<Event> create_user(const std::string& name, const std::string& color,
                                      const std::string& request_id, int avatar_id = -1) = 0;
    virtual Result<Event> update_user(const std::string& user_id, const std::string& name,
                                      const std::string& color, bool active,
                                      const std::string& request_id, int avatar_id = -1) = 0;
    virtual Result<Event> archive_user(const std::string& user_id,
                                       const std::string& request_id) = 0;
    virtual Result<Event> create_product(const std::string& name, const std::string& category,
                                         const std::string& icon, const std::string& color,
                                         Cents price_cents, int sort,
                                         const std::string& request_id) = 0;
    virtual Result<Event> update_product(const std::string& product_id, const std::string& name,
                                         const std::string& category, const std::string& icon,
                                         const std::string& color, Cents price_cents, int sort,
                                         bool active, const std::string& request_id) = 0;
    virtual Result<Event> archive_product(const std::string& product_id,
                                          const std::string& request_id) = 0;
    virtual Result<Event> delete_product(const std::string& product_id,
                                         const std::string& request_id) = 0;
    virtual Result<Event> topup(const std::string& user_id, Cents amount_cents,
                                const std::string& note, const std::string& request_id) = 0;
    virtual Result<Event> adjust(const std::string& user_id, Cents amount_cents,
                                 const std::string& note, const std::string& request_id) = 0;
    virtual Result<Event> void_event(const std::string& event_id, const std::string& reason,
                                     const std::string& request_id) = 0;
    virtual Result<Event> undo_booking(const std::string& event_id,
                                       const std::string& request_id) = 0;
    virtual Result<Event> add_expense(const std::string& category, const std::string& description,
                                      const std::string& vendor, Cents amount_cents,
                                      const std::string& note, const std::string& request_id) = 0;
    virtual Result<Event> update_expense(const std::string& expense_id, const std::string& category,
                                         const std::string& description, const std::string& vendor,
                                         Cents amount_cents, const std::string& note,
                                         const std::string& request_id) = 0;
    virtual Result<Event> void_expense(const std::string& expense_id, const std::string& reason,
                                       const std::string& request_id) = 0;
    virtual Result<Event> record_cash_count(Cents counted_cents, const std::string& note,
                                            const std::string& request_id) = 0;

    virtual std::vector<Event> events() const = 0;
    virtual std::vector<Expense> expenses() const = 0;
    virtual std::vector<CashCount> cash_counts() const = 0;

    // Read-only ledger snapshot for report generation (ADR 0013). The copy is
    // taken under the service mutex, its commit hook is cleared and it is
    // exposed through const so reports cannot mutate the live ledger.
    virtual std::shared_ptr<const Ledger> ledger_snapshot() const = 0;

    // --- Auth (single admin) ---
    virtual Result<Session> login(const std::string& username, const std::string& password,
                                  const std::string& client_ip) = 0;
    virtual Result<Unit> logout(const std::string& token) = 0;
    virtual Result<Session> validate_session(const std::string& token) = 0;
    virtual bool validate_csrf(const std::string& token, const std::string& csrf) = 0;
    virtual Result<Unit> change_admin_password(const std::string& current_password,
                                               const std::string& new_password,
                                               const std::string& request_id) = 0;
    virtual bool must_change_admin_password() const override = 0;
    // Plaintext initial password, available only until it has been changed.
    virtual std::string setup_password() const override = 0;

    // --- Settings / time / AP ---
    // Returns the effective settings for API/UI consumption. The implementation
    // MUST return a copy with all secrets cleared: `admin.password_hash`,
    // `admin.salt`, `network.ap_password` and `network.sta_password` must be
    // empty in the returned value. Stored credentials never leave the service
    // through this accessor; authentication uses the dedicated auth path.
    virtual Settings settings() const = 0;
    virtual Result<Unit> save_settings(const Settings& settings, const std::string& request_id) = 0;
    virtual Result<Unit> sync_time(Timestamp epoch_seconds, const std::string& source) = 0;
    virtual Result<Unit> start_ap() = 0;
    virtual Result<Unit> stop_ap() = 0;
    virtual Result<Unit> extend_ap(int seconds) = 0;

    // --- Backup ---
    virtual Result<std::string> export_backup() = 0;
    virtual Result<Unit> import_backup(const std::string& json, const std::string& request_id) = 0;

    // Erase the event journal, snapshots and stored settings and return to the
    // first-boot state. The caller must reset platform credentials and reboot;
    // in-memory state is cleared but no new credentials are generated here so
    // the next boot shows a fresh setup password (ADR 0013).
    virtual Result<Unit> factory_reset() = 0;

    virtual void set_change_callback(ChangeCallback callback) = 0;
    virtual void refresh() = 0;
};

}  // namespace kaffee
