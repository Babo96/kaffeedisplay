#include "kaffee/service.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "kaffee/auth.hpp"
#include "kaffee/json.hpp"

namespace kaffee {
namespace {

constexpr Timestamp kSessionIdleSeconds = 30 * 60;
constexpr Timestamp kSessionAbsoluteSeconds = 8 * 60 * 60;
constexpr int kLoginMaxFailures = 5;
constexpr uint64_t kLoginWindowSeconds = 60;
constexpr uint64_t kLoginBackoffBaseSeconds = 5;
constexpr uint64_t kLoginBackoffMaxSeconds = 300;
constexpr uint32_t kAdminIterations = 10000;
constexpr size_t kAdminSaltBytes = 16;
constexpr size_t kSessionTokenBytes = 16;
constexpr size_t kSetupPasswordLength = 16;
// Compact after this many journaled events, or when the partition is 90% full.
constexpr uint32_t kCompactEventThreshold = 256;

// ---------------------------------------------------------------------------
// Europe/Berlin day boundaries for stats. Reports already use the same rules;
// the service has no net/reports dependency, so the small calendar helpers live
// here instead of pulling in the whole report engine.
// ---------------------------------------------------------------------------

int64_t floor_div(int64_t value, int64_t divisor) {
    int64_t quotient = value / divisor;
    const int64_t remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) --quotient;
    return quotient;
}

int64_t days_from_civil(int year, int month, int day) {
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy =
        static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t days, int& year, int& month, int& day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year += (month <= 2 ? 1 : 0);
}

int weekday_from_days(int64_t days) {
    int weekday = static_cast<int>((days + 4) % 7);
    if (weekday < 0) weekday += 7;
    return weekday;
}

int64_t last_sunday_days(int year, int month) {
    const int next_year = month == 12 ? year + 1 : year;
    const int next_month = month == 12 ? 1 : month + 1;
    const int64_t last_day = days_from_civil(next_year, next_month, 1) - 1;
    return last_day - weekday_from_days(last_day);
}

int berlin_offset_seconds(Timestamp utc) {
    int year = 1970;
    int month = 1;
    int day = 1;
    civil_from_days(floor_div(utc, 86400), year, month, day);
    const int64_t start = last_sunday_days(year, 3) * 86400 + 3600;
    const int64_t end = last_sunday_days(year, 10) * 86400 + 3600;
    return (utc >= start && utc < end) ? 7200 : 3600;
}

Timestamp berlin_wall_to_utc(int64_t wall_seconds) {
    Timestamp candidate = wall_seconds - 3600;
    if (berlin_offset_seconds(candidate) == 7200) candidate = wall_seconds - 7200;
    return candidate;
}

bool berlin_day_bounds(Timestamp utc, Timestamp& start, Timestamp& end) {
    const int64_t local_days = floor_div(utc + berlin_offset_seconds(utc), 86400);
    start = berlin_wall_to_utc(local_days * 86400);
    end = berlin_wall_to_utc((local_days + 1) * 86400);
    return end > start;
}

std::string make_id(IPlatform& platform, std::string_view prefix) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(platform.random_u64()));
    return std::string(prefix) + "_" + buffer;
}

// Pick a preset uniformly from 1..30, skipping the last created user's preset
// when possible. Use the platform RNG; only the chosen ID enters the journal.
int random_user_avatar(IPlatform& platform, const std::vector<User>& users) {
    const int previous = users.empty() ? 0 : users.back().avatar_id;
    const bool skip_previous = previous >= 1 && previous <= kMaxUserAvatarId;
    const int available = skip_previous ? kMaxUserAvatarId - 1 : kMaxUserAvatarId;
    int selected = static_cast<int>(platform.random_u64() % available) + 1;
    if (skip_previous && selected >= previous) ++selected;
    return selected;
}

std::string random_hex(IPlatform& platform, size_t bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    while (out.size() < bytes * 2) {
        uint64_t value = platform.random_u64();
        for (int i = 0; i < 16 && out.size() < bytes * 2; ++i) {
            out.push_back(kHex[value & 0x0f]);
            value >>= 4;
        }
    }
    return out;
}

std::string random_password(IPlatform& platform, size_t length) {
    // Unambiguous alphabet for a password that is read from a screen.
    static constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string out;
    out.reserve(length);
    while (out.size() < length) {
        uint64_t value = platform.random_u64();
        for (int i = 0; i < 10 && out.size() < length; ++i) {
            out.push_back(kAlphabet[value % (sizeof(kAlphabet) - 1)]);
            value /= (sizeof(kAlphabet) - 1);
        }
    }
    return out;
}

Result<Unit> settings_fail(std::string message) {
    return Result<Unit>::fail("settings", std::move(message));
}

Result<Unit> validate_settings(const Settings& settings) {
    const DisplaySettings& d = settings.display;
    if (d.rotation != 0 && d.rotation != 90 && d.rotation != 180 && d.rotation != 270)
        return settings_fail("invalid rotation");
    if (d.brightness_active < 5 || d.brightness_active > 100)
        return settings_fail("brightness_active out of range");
    if (d.brightness_idle < 1 || d.brightness_idle > 100)
        return settings_fail("brightness_idle out of range");
    if (d.dim_timeout_s < 0 || d.dim_timeout_s > 86400)
        return settings_fail("dim_timeout_s out of range");
    if (d.screen_off_timeout_s < 0 || d.screen_off_timeout_s > 86400)
        return settings_fail("screen_off_timeout_s out of range");
    if (settings.network.ap_window_s < 0 || settings.network.ap_window_s > 86400)
        return settings_fail("ap_window_s out of range");
    if (settings.network.hostname.empty()) return settings_fail("hostname must not be empty");
    if (settings.time.timezone.empty()) return settings_fail("timezone must not be empty");
    if (settings.app.max_open_debt_cents < 0)
        return settings_fail("max_open_debt_cents must not be negative");
    return Result<Unit>::ok(Unit{});
}

bool same_settings(const Settings& a, const Settings& b) {
    return settings_to_json(a, true).dump() == settings_to_json(b, true).dump();
}

}  // namespace

class KaffeeServiceImpl final : public KaffeeService {
  public:
    KaffeeServiceImpl(IEventStore& store, ISettingsStore& settings_store, IPlatform& platform)
        : store_(store), settings_store_(settings_store), platform_(platform) {}
    ~KaffeeServiceImpl() override;

    Result<Unit> init();

    // --- UI-facing ---
    std::vector<User> users() const override;
    std::vector<Product> products() const override;
    std::optional<User> find_user(const std::string& id) const override;
    std::optional<Product> find_product(const std::string& id) const override;
    Cents balance(const std::string& user_id) const override;
    Summary summary() const override;
    Leaderboard leaderboard(Timestamp from, Timestamp to) const override;
    SystemStats stats() const override;
    TimeInfo time_info() const override;
    NetworkStatus network_status() const override;
    DisplaySettings display_settings() const override;
    AppSettings app_settings() const override;
    Result<Unit> apply_display_settings(const DisplaySettings& settings) override;

    Result<Event> purchase(const std::string& user_id, const std::string& product_id,
                           const std::string& request_id) override;
    Result<Event> guest_sale(const std::string& product_id, const std::string& request_id) override;

    // --- Admin operations ---
    Result<Event> create_user(const std::string& name, const std::string& color,
                              const std::string& request_id, int avatar_id) override;
    Result<Event> update_user(const std::string& user_id, const std::string& name,
                              const std::string& color, bool active, const std::string& request_id,
                              int avatar_id) override;
    Result<Event> archive_user(const std::string& user_id, const std::string& request_id) override;
    Result<Event> create_product(const std::string& name, const std::string& category,
                                 const std::string& icon, const std::string& color,
                                 Cents price_cents, int sort,
                                 const std::string& request_id) override;
    Result<Event> update_product(const std::string& product_id, const std::string& name,
                                 const std::string& category, const std::string& icon,
                                 const std::string& color, Cents price_cents, int sort, bool active,
                                 const std::string& request_id) override;
    Result<Event> archive_product(const std::string& product_id,
                                  const std::string& request_id) override;
    Result<Event> delete_product(const std::string& product_id,
                                 const std::string& request_id) override;
    Result<Event> topup(const std::string& user_id, Cents amount_cents, const std::string& note,
                        const std::string& request_id) override;
    Result<Event> adjust(const std::string& user_id, Cents amount_cents, const std::string& note,
                         const std::string& request_id) override;
    Result<Event> void_event(const std::string& event_id, const std::string& reason,
                             const std::string& request_id) override;
    Result<Event> undo_booking(const std::string& event_id, const std::string& request_id) override;
    Result<Event> add_expense(const std::string& category, const std::string& description,
                              const std::string& vendor, Cents amount_cents,
                              const std::string& note, const std::string& request_id) override;
    Result<Event> update_expense(const std::string& expense_id, const std::string& category,
                                 const std::string& description, const std::string& vendor,
                                 Cents amount_cents, const std::string& note,
                                 const std::string& request_id) override;
    Result<Event> void_expense(const std::string& expense_id, const std::string& reason,
                               const std::string& request_id) override;
    Result<Event> record_cash_count(Cents counted_cents, const std::string& note,
                                    const std::string& request_id) override;

    std::vector<Event> events() const override;
    std::vector<Expense> expenses() const override;
    std::vector<CashCount> cash_counts() const override;
    std::shared_ptr<const Ledger> ledger_snapshot() const override;

    // --- Auth ---
    Result<Session> login(const std::string& username, const std::string& password,
                          const std::string& client_ip) override;
    Result<Unit> logout(const std::string& token) override;
    Result<Session> validate_session(const std::string& token) override;
    bool validate_csrf(const std::string& token, const std::string& csrf) override;
    Result<Unit> change_admin_password(const std::string& current_password,
                                       const std::string& new_password,
                                       const std::string& request_id) override;
    bool must_change_admin_password() const override;
    std::string setup_password() const override;

    // --- Settings / time / AP ---
    Settings settings() const override;
    Result<Unit> save_settings(const Settings& settings, const std::string& request_id) override;
    Result<Unit> sync_time(Timestamp epoch_seconds, const std::string& source) override;
    Result<Unit> start_ap() override;
    Result<Unit> stop_ap() override;
    Result<Unit> extend_ap(int seconds) override;

    // --- Backup ---
    Result<std::string> export_backup() override;
    Result<Unit> import_backup(const std::string& json, const std::string& request_id) override;
    Result<Unit> factory_reset() override;

    void set_change_callback(ChangeCallback callback) override;
    void refresh() override;

  private:
    struct SessionState {
        Session session;
        uint64_t created_mono = 0;
        uint64_t last_activity_mono = 0;
    };

    struct LoginState {
        int failures = 0;
        uint64_t window_start_mono = 0;
        uint64_t blocked_until_mono = 0;
    };

    template <typename Fn>
    Result<Event> mutate(Fn fn) {
        Result<Event> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = fn();
            if (result) maybe_compact_locked();
        }
        if (result) notify_changed();
        return result;
    }

    template <typename Fn>
    Result<Unit> mutate_unit(Fn fn) {
        Result<Unit> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = fn();
            if (result) maybe_compact_locked();
        }
        if (result) notify_changed();
        return result;
    }

    Timestamp now() const {
        return platform_.time_info().now;
    }
    TimeSource time_source() const {
        return platform_.time_info().source;
    }

    // Monotonic seconds for security timeouts. Wall time is advisory only;
    // a stale or backwards clock must not extend sessions or rate limits.
    uint64_t monotonic_seconds() const {
        uint64_t value = platform_.stats().uptime_s;
        if (value == 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::steady_clock::now() - mono_epoch_)
                                     .count();
            value = elapsed >= 0 ? static_cast<uint64_t>(elapsed) + 1 : 1;
        }
        if (value < last_mono_) value = last_mono_;
        last_mono_ = value;
        return value;
    }

    std::optional<Event> existing_request_locked(const std::string& request_id) const {
        if (request_id.empty()) return std::nullopt;
        return ledger_.find_request(request_id);
    }

    void maybe_compact_locked();
    void notify_changed();
    void notifier_loop();
    void on_ap_stopped();

    bool rate_limited_locked(const std::string& client_ip, uint64_t mono) const;
    void record_login_failure_locked(const std::string& client_ip, uint64_t mono);
    bool session_expired(const SessionState& state, uint64_t mono) const;
    Timestamp session_wall_expiry(Timestamp wall) const;

    Result<Unit> audit_locked(const std::string& kind, const std::string& meta,
                              const std::string& request_id, const std::string& actor = "admin");
    Result<Unit> reload_ledger_locked();
    void rollback_settings_locked(const Settings& previous);
    bool has_password_change_audit() const;

    IEventStore& store_;
    ISettingsStore& settings_store_;
    IPlatform& platform_;

    mutable std::mutex mutex_;
    std::condition_variable notifier_cv_;
    std::thread notifier_;
    bool notify_pending_ = false;
    bool notifier_stopping_ = false;
    ChangeCallback change_callback_;

    Ledger ledger_;
    Settings settings_;
    std::map<std::string, SessionState> sessions_;
    std::map<std::string, LoginState> login_states_;
    std::string setup_password_;
    bool must_change_password_ = false;
    Timestamp last_backup_at_ = 0;
    // stats() is polled by the LVGL task. Cache the flash-backed fields for a
    // few seconds so page 3 cannot drive LittleFS/FILE allocation every tick.
    mutable StorageInfo stats_storage_;
    mutable Timestamp stats_backup_at_ = 0;
    mutable int stats_drinks_ = 0;
    mutable uint64_t stats_cache_uptime_s_ = 0;
    mutable bool stats_cache_valid_ = false;
    const std::chrono::steady_clock::time_point mono_epoch_ = std::chrono::steady_clock::now();
    mutable uint64_t last_mono_ = 0;
};

// ---------------------------------------------------------------------------
// Construction / notifier

KaffeeServiceImpl::~KaffeeServiceImpl() {
    platform_.set_ap_stopped_callback({});
    {
        std::lock_guard<std::mutex> lock(mutex_);
        notifier_stopping_ = true;
    }
    notifier_cv_.notify_all();
    if (notifier_.joinable()) notifier_.join();
}

void KaffeeServiceImpl::notifier_loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        notifier_cv_.wait(lock, [this] { return notifier_stopping_ || notify_pending_; });
        if (notifier_stopping_) break;
        notify_pending_ = false;
        ChangeCallback callback = change_callback_;
        lock.unlock();
        platform_.notify_ledger_changed();
        if (callback) callback();
        lock.lock();
    }
}

void KaffeeServiceImpl::notify_changed() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_cache_valid_ = false;
    notify_pending_ = true;
    notifier_cv_.notify_one();
}

void KaffeeServiceImpl::on_ap_stopped() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
    login_states_.clear();
}

Result<Unit> KaffeeServiceImpl::init() {
    auto mounted = store_.mount();
    if (!mounted) return mounted;

    ledger_ = Ledger([this](std::string_view prefix) { return make_id(platform_, prefix); });
    auto loaded = store_.load(ledger_);
    if (!loaded) return Result<Unit>::fail(loaded.error->code, loaded.error->message);

    // A settings read failure is fatal: never overwrite credentials we could
    // not read.
    auto stored = settings_store_.load();
    if (!stored) return Result<Unit>::fail(stored.error->code, stored.error->message);
    settings_ = *stored.value;

    if (settings_.admin.username.empty()) settings_.admin.username = "admin";

    bool dirty = false;
    bool setup_changed = false;
    const bool has_credentials =
        !settings_.admin.password_hash.empty() && !settings_.admin.salt.empty();
    if (!has_credentials) {
        const std::string initial = random_password(platform_, kSetupPasswordLength);
        settings_.admin.salt = random_hex(platform_, kAdminSaltBytes);
        settings_.admin.iterations = kAdminIterations;
        settings_.admin.password_hash =
            pbkdf2_sha256_hex(initial, settings_.admin.salt, settings_.admin.iterations);
        setup_password_ = initial;
        must_change_password_ = true;
        setup_changed = true;
        dirty = true;
    } else {
        must_change_password_ = !has_password_change_audit();
        if (must_change_password_) {
            auto stored_setup = settings_store_.load_setup_password();
            if (!stored_setup) {
                return Result<Unit>::fail(stored_setup.error->code, stored_setup.error->message);
            }
            setup_password_ = *stored_setup.value;
            if (setup_password_.empty()) {
                // Upgrade from firmware that kept the password in RAM only, or
                // a damaged key. Rotate the admin credentials so the device is
                // not permanently locked out of the web UI.
                const std::string initial = random_password(platform_, kSetupPasswordLength);
                settings_.admin.salt = random_hex(platform_, kAdminSaltBytes);
                settings_.admin.iterations = kAdminIterations;
                settings_.admin.password_hash =
                    pbkdf2_sha256_hex(initial, settings_.admin.salt, settings_.admin.iterations);
                setup_password_ = initial;
                setup_changed = true;
                dirty = true;
            }
        } else {
            setup_password_.clear();
            // Remove a stale key after a completed password change.
            (void)settings_store_.clear_setup_password();
        }
    }

    if (settings_.network.ap_password.empty()) {
        settings_.network.ap_password = random_password(platform_, 12);
        dirty = true;
    }
    // Persist the setup password before credentials: if the settings write
    // fails, the next boot regenerates credentials and overwrites this key.
    if (setup_changed) {
        auto saved_setup = settings_store_.save_setup_password(setup_password_);
        if (!saved_setup) return saved_setup;
    }
    if (dirty) {
        auto saved = settings_store_.save(settings_);
        if (!saved) return saved;
    }

    ledger_.set_commit_hook(
        CommitHook{[this](const Event& event) { return store_.append(event); }});
    maybe_compact_locked();
    platform_.set_ap_stopped_callback([this] { on_ap_stopped(); });
    try {
        notifier_ = std::thread([this] { notifier_loop(); });
    } catch (const std::exception& e) {
        return Result<Unit>::fail("thread", e.what());
    }
    return Result<Unit>::ok(Unit{});
}

bool KaffeeServiceImpl::has_password_change_audit() const {
    return std::any_of(ledger_.events().begin(), ledger_.events().end(), [](const Event& event) {
        return event.type == EventType::Audit && event.note == "admin_password_changed";
    });
}

Result<Unit> KaffeeServiceImpl::audit_locked(const std::string& kind, const std::string& meta,
                                             const std::string& request_id,
                                             const std::string& actor) {
    auto result = ledger_.audit(kind, meta, actor, request_id, now(), time_source());
    if (!result) return Result<Unit>::fail(result.error->code, result.error->message);
    return Result<Unit>::ok(Unit{});
}

void KaffeeServiceImpl::maybe_compact_locked() {
    const StorageInfo info = store_.info();
    bool due = info.journal_events >= kCompactEventThreshold;
    if (!due && info.total_bytes > 0) {
        due = info.used_bytes >= info.total_bytes - info.total_bytes / 10;
    }
    if (!due) return;
    // Failure keeps the journal; compaction is retried on a later mutation.
    store_.compact(ledger_);
}

Result<Unit> KaffeeServiceImpl::reload_ledger_locked() {
    ledger_.reset();
    auto loaded = store_.load(ledger_);
    if (!loaded) return Result<Unit>::fail(loaded.error->code, loaded.error->message);
    return Result<Unit>::ok(Unit{});
}

void KaffeeServiceImpl::rollback_settings_locked(const Settings& previous) {
    settings_ = previous;
    settings_store_.save(previous);
    platform_.apply_display_settings(previous.display);
    platform_.apply_network_settings(previous.network);
    platform_.apply_time_settings(previous.time);
}

// ---------------------------------------------------------------------------
// Queries

std::vector<User> KaffeeServiceImpl::users() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.users();
}

std::vector<Product> KaffeeServiceImpl::products() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.products();
}

std::optional<User> KaffeeServiceImpl::find_user(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.find_user(id);
}

std::optional<Product> KaffeeServiceImpl::find_product(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.find_product(id);
}

Cents KaffeeServiceImpl::balance(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.balance(user_id);
}

Summary KaffeeServiceImpl::summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.summary();
}

Leaderboard KaffeeServiceImpl::leaderboard(Timestamp from, Timestamp to) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.leaderboard(from, to);
}

SystemStats KaffeeServiceImpl::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    SystemStats out = platform_.stats();
    out.time = platform_.time_info();

    const uint64_t uptime = out.uptime_s;
    const bool refresh = !stats_cache_valid_ || uptime < stats_cache_uptime_s_ ||
                         uptime - stats_cache_uptime_s_ >= 3;
    if (refresh) {
        stats_storage_ = store_.info();
        stats_backup_at_ = last_backup_at_;
        stats_drinks_ = 0;
        const Timestamp ts = out.time.now;
        Timestamp day_start = 0;
        Timestamp day_end = 0;
        if (ts > 0 && berlin_day_bounds(ts, day_start, day_end)) {
            int drinks = 0;
            for (const Event& event : ledger_.events()) {
                if (event.ts < day_start || event.ts >= day_end) continue;
                if (event.type != EventType::Purchase && event.type != EventType::GuestSale)
                    continue;
                // A void keeps the original event in the append-only journal,
                // but it no longer counts as a drink sold today.
                if (ledger_.is_voided(event.id)) continue;
                ++drinks;
            }
            stats_drinks_ = drinks;
        }
        stats_cache_uptime_s_ = uptime;
        stats_cache_valid_ = true;
    }

    out.storage = stats_storage_;
    out.last_backup_at = stats_backup_at_;
    out.drinks_today = stats_drinks_;
    return out;
}

TimeInfo KaffeeServiceImpl::time_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return platform_.time_info();
}

NetworkStatus KaffeeServiceImpl::network_status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return platform_.network_status();
}

DisplaySettings KaffeeServiceImpl::display_settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.display;
}

AppSettings KaffeeServiceImpl::app_settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.app;
}

Result<Unit> KaffeeServiceImpl::apply_display_settings(const DisplaySettings& display) {
    return mutate_unit([&]() -> Result<Unit> {
        Settings candidate = settings_;
        candidate.display = display;
        auto valid = validate_settings(candidate);
        if (!valid) return valid;
        auto applied = platform_.apply_display_settings(display);
        if (!applied) return applied;
        settings_.display = display;
        return settings_store_.save(settings_);
    });
}

// ---------------------------------------------------------------------------
// Mutations

Result<Event> KaffeeServiceImpl::purchase(const std::string& user_id, const std::string& product_id,
                                          const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);

        const auto product = ledger_.find_product(product_id);
        const auto user = ledger_.find_user(user_id);
        if (product && user) {
            if (settings_.app.max_open_debt_cents > 0 && product->active && user->active) {
                const Cents after = ledger_.balance(user_id) - product->price_cents;
                if (after < -static_cast<Cents>(settings_.app.max_open_debt_cents)) {
                    return Result<Event>::fail("debt", "maximum open debt exceeded");
                }
            }
        }
        return ledger_.purchase(user_id, product_id, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::guest_sale(const std::string& product_id,
                                            const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.guest_sale(product_id, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::create_user(const std::string& name, const std::string& color,
                                             const std::string& request_id, int avatar_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        const int selected_avatar =
            avatar_id == -1 ? random_user_avatar(platform_, ledger_.users()) : avatar_id;
        return ledger_.create_user(name, color, request_id, now(), time_source(), selected_avatar);
    });
}

Result<Event> KaffeeServiceImpl::update_user(const std::string& user_id, const std::string& name,
                                             const std::string& color, bool active,
                                             const std::string& request_id, int avatar_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.update_user(user_id, name, color, active, request_id, now(), time_source(),
                                   avatar_id);
    });
}

Result<Event> KaffeeServiceImpl::archive_user(const std::string& user_id,
                                              const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.archive_user(user_id, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::create_product(const std::string& name,
                                                const std::string& category,
                                                const std::string& icon, const std::string& color,
                                                Cents price_cents, int sort,
                                                const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.create_product(name, category, icon, color, price_cents, sort, request_id,
                                      now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::update_product(const std::string& product_id,
                                                const std::string& name,
                                                const std::string& category,
                                                const std::string& icon, const std::string& color,
                                                Cents price_cents, int sort, bool active,
                                                const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.update_product(product_id, name, category, icon, color, price_cents, sort,
                                      active, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::archive_product(const std::string& product_id,
                                                 const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.archive_product(product_id, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::delete_product(const std::string& product_id,
                                                const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.delete_product(product_id, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::topup(const std::string& user_id, Cents amount_cents,
                                       const std::string& note, const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.topup(user_id, amount_cents, note, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::adjust(const std::string& user_id, Cents amount_cents,
                                        const std::string& note, const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        if (amount_cents < 0 && settings_.app.max_open_debt_cents > 0 &&
            ledger_.find_user(user_id)) {
            const Cents after = ledger_.balance(user_id) + amount_cents;
            if (after < -static_cast<Cents>(settings_.app.max_open_debt_cents)) {
                return Result<Event>::fail("debt", "maximum open debt exceeded");
            }
        }
        return ledger_.adjust(user_id, amount_cents, note, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::void_event(const std::string& event_id, const std::string& reason,
                                            const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.void_event(event_id, reason, request_id, now(), time_source(), "admin");
    });
}

Result<Event> KaffeeServiceImpl::undo_booking(const std::string& event_id,
                                              const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        const auto original = ledger_.find_event(event_id);
        if (!original) return Result<Event>::fail("event", "unknown event");
        if (original->type != EventType::Purchase)
            return Result<Event>::fail("void", "only member bookings can be undone");
        return ledger_.void_event(event_id, "undo", request_id, now(), time_source(), "ui");
    });
}

Result<Event> KaffeeServiceImpl::add_expense(const std::string& category,
                                             const std::string& description,
                                             const std::string& vendor, Cents amount_cents,
                                             const std::string& note,
                                             const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.add_expense(category, description, vendor, amount_cents, note, request_id,
                                   now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::update_expense(const std::string& expense_id,
                                                const std::string& category,
                                                const std::string& description,
                                                const std::string& vendor, Cents amount_cents,
                                                const std::string& note,
                                                const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.update_expense(expense_id, category, description, vendor, amount_cents, note,
                                      request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::void_expense(const std::string& expense_id,
                                              const std::string& reason,
                                              const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.void_expense(expense_id, reason, request_id, now(), time_source());
    });
}

Result<Event> KaffeeServiceImpl::record_cash_count(Cents counted_cents, const std::string& note,
                                                   const std::string& request_id) {
    return mutate([&]() -> Result<Event> {
        if (auto existing = existing_request_locked(request_id))
            return Result<Event>::ok(*existing);
        return ledger_.record_cash_count(counted_cents, note, request_id, now(), time_source());
    });
}

std::vector<Event> KaffeeServiceImpl::events() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.events();
}

std::vector<Expense> KaffeeServiceImpl::expenses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.expenses();
}

std::vector<CashCount> KaffeeServiceImpl::cash_counts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ledger_.cash_counts();
}

std::shared_ptr<const Ledger> KaffeeServiceImpl::ledger_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto snapshot = std::make_shared<Ledger>(ledger_);
    snapshot->clear_commit_hook();
    return snapshot;
}

// ---------------------------------------------------------------------------
// Auth

bool KaffeeServiceImpl::rate_limited_locked(const std::string& client_ip, uint64_t mono) const {
    auto it = login_states_.find(client_ip);
    if (it == login_states_.end()) return false;
    return it->second.blocked_until_mono > 0 && mono < it->second.blocked_until_mono;
}

void KaffeeServiceImpl::record_login_failure_locked(const std::string& client_ip, uint64_t mono) {
    LoginState& state = login_states_[client_ip];
    const bool new_window = state.window_start_mono == 0 || mono < state.window_start_mono ||
                            mono - state.window_start_mono >= kLoginWindowSeconds;
    if (new_window) {
        state.window_start_mono = mono;
        state.failures = 0;
    }
    ++state.failures;
    if (state.failures >= kLoginMaxFailures) {
        const int exponent = std::min(state.failures - kLoginMaxFailures, 6);
        uint64_t delay = kLoginBackoffBaseSeconds << exponent;
        if (delay > kLoginBackoffMaxSeconds) delay = kLoginBackoffMaxSeconds;
        state.blocked_until_mono = mono + delay;
        if (state.failures == kLoginMaxFailures) {
            // Persist one audit event when the lockout is first triggered; do
            // not write an event for every failed attempt.
            auto audit =
                ledger_.audit("admin_login_locked", "{}", "system", "", now(), time_source());
            if (audit) {
                notify_pending_ = true;
                notifier_cv_.notify_one();
            }
        }
    }
}

bool KaffeeServiceImpl::session_expired(const SessionState& state, uint64_t mono) const {
    if (mono == 0) return false;
    if (mono >= state.created_mono + static_cast<uint64_t>(kSessionAbsoluteSeconds)) return true;
    return mono >= state.last_activity_mono + static_cast<uint64_t>(kSessionIdleSeconds);
}

Timestamp KaffeeServiceImpl::session_wall_expiry(Timestamp wall) const {
    if (wall <= 0) return 0;
    return wall + kSessionIdleSeconds;
}

Result<Session> KaffeeServiceImpl::login(const std::string& username, const std::string& password,
                                         const std::string& client_ip) {
    AdminCredentials credentials;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (rate_limited_locked(client_ip, monotonic_seconds())) {
            return Result<Session>::fail("rate_limit", "too many attempts, try again later");
        }
        credentials = settings_.admin;
    }

    // PBKDF2 intentionally runs outside the global service mutex.
    const std::string computed =
        pbkdf2_sha256_hex(password, credentials.salt, credentials.iterations);

    std::unique_lock<std::mutex> lock(mutex_);
    const uint64_t mono = monotonic_seconds();
    const Timestamp wall = now();
    if (rate_limited_locked(client_ip, mono)) {
        return Result<Session>::fail("rate_limit", "too many attempts, try again later");
    }

    const bool user_ok = constant_time_equal(username, settings_.admin.username);
    const bool password_ok = constant_time_equal(computed, settings_.admin.password_hash);
    if (!user_ok || !password_ok) {
        record_login_failure_locked(client_ip, mono);
        return Result<Session>::fail("auth", "invalid credentials");
    }

    // Rehash credentials created before the iteration-count change. The new
    // hash is computed outside the mutex; the save is skipped if another
    // request changed the credentials meanwhile.
    if (settings_.admin.iterations != kAdminIterations) {
        const std::string new_salt = random_hex(platform_, kAdminSaltBytes);
        const std::string old_hash = settings_.admin.password_hash;
        const std::string old_salt = settings_.admin.salt;
        lock.unlock();
        const std::string new_hash = pbkdf2_sha256_hex(password, new_salt, kAdminIterations);
        lock.lock();
        if (settings_.admin.password_hash == old_hash && settings_.admin.salt == old_salt) {
            Settings updated = settings_;
            updated.admin.salt = new_salt;
            updated.admin.iterations = kAdminIterations;
            updated.admin.password_hash = new_hash;
            if (settings_store_.save(updated)) settings_ = updated;
        }
    }

    login_states_.erase(client_ip);
    Session session;
    session.token = random_hex(platform_, kSessionTokenBytes);
    session.csrf_token = random_hex(platform_, kSessionTokenBytes);
    session.created_at = wall;
    session.expires_at = session_wall_expiry(wall);
    sessions_[session.token] = SessionState{session, mono, mono};
    return Result<Session>::ok(session);
}

Result<Unit> KaffeeServiceImpl::logout(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(token);
    return Result<Unit>::ok(Unit{});
}

Result<Session> KaffeeServiceImpl::validate_session(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return Result<Session>::fail("session", "invalid session");
    const uint64_t mono = monotonic_seconds();
    if (session_expired(it->second, mono)) {
        sessions_.erase(it);
        return Result<Session>::fail("session", "session expired");
    }
    it->second.last_activity_mono = mono;
    it->second.session.expires_at = session_wall_expiry(now());
    return Result<Session>::ok(it->second.session);
}

bool KaffeeServiceImpl::validate_csrf(const std::string& token, const std::string& csrf) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return false;
    if (session_expired(it->second, monotonic_seconds())) {
        sessions_.erase(it);
        return false;
    }
    // The CSRF token is a separate random value, never the session token.
    return constant_time_equal(csrf, it->second.session.csrf_token);
}

Result<Unit> KaffeeServiceImpl::change_admin_password(const std::string& current_password,
                                                      const std::string& new_password,
                                                      const std::string& request_id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto existing = existing_request_locked(request_id)) return Result<Unit>::ok(Unit{});
    }

    if (new_password.size() < 10)
        return Result<Unit>::fail("password", "new password must have at least 10 characters");
    if (new_password.size() > 256)
        return Result<Unit>::fail("password", "new password is too long");
    if (constant_time_equal(new_password, current_password))
        return Result<Unit>::fail("password", "new password must differ from the current one");

    AdminCredentials snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = settings_.admin;
    }

    // Password verification and derivation run outside the global mutex.
    const std::string current_hash =
        pbkdf2_sha256_hex(current_password, snapshot.salt, snapshot.iterations);
    std::string new_salt;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        new_salt = random_hex(platform_, kAdminSaltBytes);
    }
    const std::string new_hash = pbkdf2_sha256_hex(new_password, new_salt, kAdminIterations);

    Result<Unit> result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto existing = existing_request_locked(request_id)) return Result<Unit>::ok(Unit{});
        if (settings_.admin.password_hash != snapshot.password_hash ||
            settings_.admin.salt != snapshot.salt) {
            return Result<Unit>::fail("auth", "credentials changed, retry");
        }
        if (!constant_time_equal(current_hash, settings_.admin.password_hash)) {
            return Result<Unit>::fail("auth", "current password is incorrect");
        }

        Settings updated = settings_;
        updated.admin.username = "admin";
        updated.admin.salt = new_salt;
        updated.admin.iterations = kAdminIterations;
        updated.admin.password_hash = new_hash;
        auto saved = settings_store_.save(updated);
        if (!saved) return saved;

        settings_ = updated;
        setup_password_.clear();
        must_change_password_ = false;
        // Best effort: the in-memory password is gone and a reboot with a
        // completed audit record will not load this key again. If NVS erasure
        // fails, the stale key is ignored and removed on the next boot.
        (void)settings_store_.clear_setup_password();
        result = audit_locked("admin_password_changed", "{}", request_id, "admin");
        if (result) maybe_compact_locked();
    }
    if (result) notify_changed();
    return result;
}

bool KaffeeServiceImpl::must_change_admin_password() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return must_change_password_;
}

std::string KaffeeServiceImpl::setup_password() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return setup_password_;
}

// ---------------------------------------------------------------------------
// Settings / time / AP

Settings KaffeeServiceImpl::settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Settings out = settings_;
    out.admin.password_hash.clear();
    out.admin.salt.clear();
    out.network.ap_password.clear();
    out.network.sta_password.clear();
    return out;
}

Result<Unit> KaffeeServiceImpl::save_settings(const Settings& input,
                                              const std::string& request_id) {
    return mutate_unit([&]() -> Result<Unit> {
        if (auto existing = existing_request_locked(request_id)) return Result<Unit>::ok(Unit{});

        Settings merged = settings_;
        merged.display = input.display;
        merged.network.ap_ssid = input.network.ap_ssid;
        merged.network.ap_window_s = input.network.ap_window_s;
        merged.network.hostname = input.network.hostname;
        merged.network.sta_enabled = input.network.sta_enabled;
        merged.network.sta_ssid = input.network.sta_ssid;
        merged.network.napt_enabled = input.network.napt_enabled;
        if (!input.network.ap_password.empty())
            merged.network.ap_password = input.network.ap_password;
        if (!input.network.sta_password.empty())
            merged.network.sta_password = input.network.sta_password;
        merged.time = input.time;
        merged.app = input.app;
        // Admin credentials are only changed through change_admin_password().

        auto valid = validate_settings(merged);
        if (!valid) return valid;

        // Persist first, then apply. A platform apply failure restores the
        // previous settings blob and live state; a save failure leaves the
        // device untouched.
        const Settings previous = settings_;
        auto rollback = [&]() {
            (void)settings_store_.save(previous);
            (void)platform_.apply_display_settings(previous.display);
            (void)platform_.apply_network_settings(previous.network);
            (void)platform_.apply_time_settings(previous.time);
        };

        auto saved = settings_store_.save(merged);
        if (!saved) return saved;
        auto applied = platform_.apply_display_settings(merged.display);
        if (!applied) {
            rollback();
            return applied;
        }
        auto network = platform_.apply_network_settings(merged.network);
        if (!network) {
            rollback();
            return network;
        }
        auto time = platform_.apply_time_settings(merged.time);
        if (!time) {
            rollback();
            return time;
        }

        settings_ = merged;
        const std::string meta = settings_to_json(settings_, false).dump();
        return audit_locked("settings_updated", meta, request_id, "admin");
    });
}

Result<Unit> KaffeeServiceImpl::sync_time(Timestamp epoch_seconds, const std::string& source) {
    if (epoch_seconds <= 0) return Result<Unit>::fail("time", "invalid epoch");
    Result<Unit> result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = platform_.sync_time(epoch_seconds, source);
    }
    if (result) notify_changed();
    return result;
}

Result<Unit> KaffeeServiceImpl::start_ap() {
    std::lock_guard<std::mutex> lock(mutex_);
    return platform_.start_ap();
}

Result<Unit> KaffeeServiceImpl::stop_ap() {
    // Do not touch session state from the caller (httpd/serial) task. The
    // network listener calls on_ap_stopped() on its worker after the AP is
    // confirmed down; that is the single deferred session-clear path.
    return platform_.stop_ap();
}

Result<Unit> KaffeeServiceImpl::extend_ap(int seconds) {
    if (seconds <= 0) return Result<Unit>::fail("ap", "duration must be positive");
    std::lock_guard<std::mutex> lock(mutex_);
    return platform_.extend_ap(seconds);
}

// ---------------------------------------------------------------------------
// Backup

Result<std::string> KaffeeServiceImpl::export_backup() {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json users = nlohmann::json::array();
    for (const User& user : ledger_.users()) users.push_back(user_to_json(user));
    nlohmann::json products = nlohmann::json::array();
    for (const Product& product : ledger_.products()) products.push_back(product_to_json(product));
    nlohmann::json events = nlohmann::json::array();
    for (const Event& event : ledger_.events()) events.push_back(event_to_json(event));
    nlohmann::json expenses_json = nlohmann::json::array();
    for (const Expense& expense : ledger_.expenses())
        expenses_json.push_back(expense_to_json(expense));
    nlohmann::json cash_counts = nlohmann::json::array();
    for (const CashCount& count : ledger_.cash_counts())
        cash_counts.push_back(cash_count_to_json(count));

    const nlohmann::json backup{{"format", "kaffeekasse-backup"},
                                {"version", 1},
                                {"created_at", now()},
                                {"settings", settings_to_json(settings_, false)},
                                {"users", std::move(users)},
                                {"products", std::move(products)},
                                {"events", std::move(events)},
                                {"expenses", std::move(expenses_json)},
                                {"cash_counts", std::move(cash_counts)}};

    last_backup_at_ = now();
    stats_cache_valid_ = false;
    return Result<std::string>::ok(backup.dump(2));
}

Result<Unit> KaffeeServiceImpl::import_backup(const std::string& json,
                                              const std::string& request_id) {
    return mutate_unit([&]() -> Result<Unit> {
        if (auto existing = existing_request_locked(request_id)) return Result<Unit>::ok(Unit{});

        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(json);
        } catch (const std::exception& e) {
            return Result<Unit>::fail("backup", std::string("invalid JSON: ") + e.what());
        }
        if (!parsed.is_object()) return Result<Unit>::fail("backup", "backup is not an object");
        const int version = parsed.value("version", 0);
        if (version < 1 || version > 1)
            return Result<Unit>::fail("backup", "unsupported backup version");
        if (!parsed.contains("events") || !parsed["events"].is_array())
            return Result<Unit>::fail("backup", "backup has no events");

        std::vector<Event> events;
        try {
            events.reserve(parsed["events"].size());
            for (const auto& item : parsed["events"]) events.push_back(event_from_json(item));
        } catch (const std::exception& e) {
            return Result<Unit>::fail("backup", std::string("invalid event: ") + e.what());
        }

        // Validate and persist settings first; the ledger is only replaced once
        // the non-secret settings are safely stored.
        const Settings previous_settings = settings_;
        Settings restored_settings = settings_;
        if (parsed.contains("settings") && parsed["settings"].is_object()) {
            restored_settings = settings_from_json(parsed["settings"], settings_);
            restored_settings.admin = settings_.admin;  // never from a backup
            auto valid = validate_settings(restored_settings);
            if (!valid) return valid;
        }
        const bool settings_changed = !same_settings(previous_settings, restored_settings);
        if (settings_changed) {
            auto saved = settings_store_.save(restored_settings);
            if (!saved) return saved;
            settings_ = restored_settings;
        }

        ledger_.reset();
        for (const Event& event : events) {
            auto applied = ledger_.apply(event);
            if (!applied) {
                auto reloaded = reload_ledger_locked();
                rollback_settings_locked(previous_settings);
                if (!reloaded)
                    return Result<Unit>::fail("backup", "invalid event chain and reload failed: " +
                                                            applied.error->message);
                return Result<Unit>::fail("backup",
                                          "invalid event chain: " + applied.error->message);
            }
        }

        auto compacted = store_.compact(ledger_);
        if (!compacted) {
            auto reloaded = reload_ledger_locked();
            rollback_settings_locked(previous_settings);
            if (!reloaded)
                return Result<Unit>::fail(
                    "backup", "compaction failed and reload failed: " + compacted.error->message);
            return compacted;
        }

        platform_.apply_display_settings(settings_.display);
        platform_.apply_network_settings(settings_.network);
        platform_.apply_time_settings(settings_.time);
        auto audit = audit_locked("backup_restored", "{}", request_id, "admin");
        if (!audit) return audit;
        return Result<Unit>::ok(Unit{});
    });
}

Result<Unit> KaffeeServiceImpl::factory_reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
    login_states_.clear();

    auto erased_store = store_.factory_reset();
    if (!erased_store) return erased_store;
    auto erased_settings = settings_store_.reset();
    if (!erased_settings) return erased_settings;

    // Do not generate replacement credentials here: the next boot must show a
    // fresh setup password through the first-boot path (ADR 0013).
    settings_ = Settings{};
    setup_password_.clear();
    must_change_password_ = true;
    last_backup_at_ = 0;
    stats_cache_valid_ = false;
    ledger_.reset();
    return Result<Unit>::ok(Unit{});
}

void KaffeeServiceImpl::set_change_callback(ChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    change_callback_ = std::move(callback);
    if (notify_pending_) notifier_cv_.notify_one();
}
void KaffeeServiceImpl::refresh() {
    notify_changed();
}

// ---------------------------------------------------------------------------
// Factory

Result<std::unique_ptr<KaffeeService>> KaffeeService::create(IEventStore& store,
                                                             ISettingsStore& settings_store,
                                                             IPlatform& platform) {
    auto service =
        std::unique_ptr<KaffeeServiceImpl>(new KaffeeServiceImpl(store, settings_store, platform));
    auto initialized = service->init();
    if (!initialized) {
        return Result<std::unique_ptr<KaffeeService>>::fail(initialized.error->code,
                                                            initialized.error->message);
    }
    return Result<std::unique_ptr<KaffeeService>>::ok(std::move(service));
}

}  // namespace kaffee
