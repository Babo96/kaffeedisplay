#include <doctest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <unistd.h>

#include "kaffee/auth.hpp"
#include "kaffee/file_store.hpp"
#include "kaffee/json.hpp"
#include "kaffee/service.hpp"

using namespace kaffee;
namespace fs = std::filesystem;

namespace {

constexpr Timestamp kNow = 1'700'000'000;
constexpr const char* kPassword = "correct horse battery";

std::atomic<int> g_temp_counter{0};

size_t open_fd_count() {
    std::error_code ec;
    size_t count = 0;
    for (const auto& entry : fs::directory_iterator("/proc/self/fd", ec)) {
        (void)entry;
        ++count;
    }
    return ec ? 0 : count;
}

struct TempDir {
    fs::path path;

    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() /
               ("kaffee-svc-" + std::to_string(::getpid()) + "-" + std::to_string(stamp) + "-" +
                std::to_string(++g_temp_counter));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

class FakePlatform : public IPlatform {
  public:
    SystemStats stats() const override {
        return stats_;
    }
    TimeInfo time_info() const override {
        return time;
    }
    uint64_t random_u64() override {
        rng += 0x9e3779b97f4a7c15ull;
        uint64_t z = rng;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    Result<Unit> sync_time(Timestamp epoch_seconds, const std::string& source) override {
        time.now = epoch_seconds;
        time.source = TimeSource::Synced;
        time.last_sync = epoch_seconds;
        time.last_source = source;
        time.trusted = true;
        synced_epoch = epoch_seconds;
        return Result<Unit>::ok(Unit{});
    }
    NetworkStatus network_status() const override {
        return network;
    }
    Result<Unit> start_ap() override {
        network.ap_active = true;
        ++ap_starts;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> stop_ap() override {
        network.ap_active = false;
        ++ap_stops;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> extend_ap(int seconds) override {
        last_extend = seconds;
        return Result<Unit>::ok(Unit{});
    }
    DisplaySettings display_settings() const override {
        return display;
    }
    Result<Unit> apply_display_settings(const DisplaySettings& settings) override {
        display = settings;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> show_display_test() override {
        ++display_tests;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> apply_network_settings(const NetworkSettings& settings) override {
        network_settings = settings;
        ++network_applies;
        if (fail_network_apply) return Result<Unit>::fail("network", "forced apply failure");
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> apply_time_settings(const TimeSettings& settings) override {
        time_settings = settings;
        ++time_applies;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> reset_network_credentials() override {
        ++network_resets;
        return Result<Unit>::ok(Unit{});
    }
    void notify_ledger_changed() override {
        ++notifications;
    }
    void set_ap_stopped_callback(std::function<void()> callback) override {
        ap_stopped_callback = std::move(callback);
    }

    void simulate_ap_timeout() {
        network.ap_active = false;
        if (ap_stopped_callback) ap_stopped_callback();
    }

    TimeInfo time;
    NetworkStatus network;
    DisplaySettings display;
    SystemStats stats_;
    NetworkSettings network_settings;
    TimeSettings time_settings;
    std::function<void()> ap_stopped_callback;
    uint64_t rng = 0x123456789abcdef0ull;
    Timestamp synced_epoch = 0;
    int ap_starts = 0;
    int ap_stops = 0;
    int last_extend = 0;
    int display_tests = 0;
    int notifications = 0;
    int network_applies = 0;
    int time_applies = 0;
    int network_resets = 0;
    bool fail_network_apply = false;
};

class FailingSettingsStore final : public ISettingsStore {
  public:
    Result<Settings> load() override {
        if (fail_load) return Result<Settings>::fail("settings", "forced load failure");
        return inner.load();
    }
    Result<Unit> save(const Settings& settings) override {
        if (fail_save) return Result<Unit>::fail("settings", "forced save failure");
        return inner.save(settings);
    }
    Result<Unit> reset() override {
        return inner.reset();
    }
    Result<std::string> load_setup_password() override {
        return inner.load_setup_password();
    }
    Result<Unit> save_setup_password(const std::string& password) override {
        return inner.save_setup_password(password);
    }
    Result<Unit> clear_setup_password() override {
        return inner.clear_setup_password();
    }

    const Settings& stored() const {
        return inner.stored();
    }

    MemorySettingsStore inner;
    bool fail_load = false;
    bool fail_save = false;
};

Event must_event(Result<Event> result) {
    REQUIRE(static_cast<bool>(result));
    return *result.value;
}

void configure_platform(FakePlatform& platform) {
    platform.time.now = kNow;
    platform.time.source = TimeSource::Synced;
    platform.time.last_sync = kNow;
    platform.time.trusted = true;
    platform.stats_.uptime_s = 1000;
}

struct ServiceFixture {
    TempDir dir;
    FileEventStore store;
    MemorySettingsStore settings_store;
    FakePlatform platform;
    std::unique_ptr<KaffeeService> service;

    explicit ServiceFixture(bool seed_known_password = false)
        : store((dir.path / "events").string()) {
        configure_platform(platform);
        if (seed_known_password) {
            Settings settings;
            settings.admin.username = "admin";
            settings.admin.salt = "00112233445566778899aabbccddeeff";
            settings.admin.iterations = 1000;
            settings.admin.password_hash =
                pbkdf2_sha256_hex(kPassword, settings.admin.salt, settings.admin.iterations);
            settings.network.ap_password = "ap-secret";
            REQUIRE(static_cast<bool>(settings_store.save(settings)));
            REQUIRE(static_cast<bool>(settings_store.save_setup_password(kPassword)));
        }
        auto created = KaffeeService::create(store, settings_store, platform);
        REQUIRE(static_cast<bool>(created));
        service = std::move(*created.value);
    }

    void advance(Timestamp seconds) {
        platform.time.now += seconds;
        platform.stats_.uptime_s += static_cast<uint64_t>(seconds);
    }

    std::string user(const std::string& name, const std::string& request_id = "") {
        return must_event(service->create_user(name, "#D4A373", request_id)).user_id;
    }
    std::string product(const std::string& name, Cents price, const std::string& request_id = "") {
        return must_event(
                   service->create_product(name, "Kaffee", "cup", "#D4A373", price, 0, request_id))
            .product_id;
    }
};

}  // namespace

TEST_CASE("first boot generates a setup password") {
    ServiceFixture fixture;
    CHECK(fixture.service->must_change_admin_password());
    const std::string initial = fixture.service->setup_password();
    CHECK(initial.size() == 16);

    CHECK_FALSE(static_cast<bool>(fixture.service->login("admin", "wrong", "10.0.0.1")));
    auto session = fixture.service->login("admin", initial, "10.0.0.2");
    REQUIRE(static_cast<bool>(session));
    CHECK(session.value->token.size() == 32);
    CHECK(session.value->csrf_token.size() == 32);
    CHECK(session.value->token != session.value->csrf_token);
}

TEST_CASE("setup password survives a service reload") {
    ServiceFixture fixture;
    const std::string initial = fixture.service->setup_password();
    REQUIRE(initial.size() == 16);
    CHECK(fixture.settings_store.stored_setup_password() == initial);

    fixture.service.reset();
    auto created = KaffeeService::create(fixture.store, fixture.settings_store, fixture.platform);
    REQUIRE(static_cast<bool>(created));
    fixture.service = std::move(*created.value);
    CHECK(fixture.service->must_change_admin_password());
    CHECK(fixture.service->setup_password() == initial);
    CHECK(static_cast<bool>(fixture.service->login("admin", initial, "10.0.0.9")));
}

TEST_CASE("changing the admin password clears the persisted setup password") {
    ServiceFixture fixture(true);
    REQUIRE(fixture.service->setup_password() == kPassword);
    REQUIRE(static_cast<bool>(
        fixture.service->change_admin_password(kPassword, "changed-password-1", "setup-1")));
    CHECK_FALSE(fixture.service->must_change_admin_password());
    CHECK(fixture.service->setup_password().empty());
    CHECK(fixture.settings_store.stored_setup_password().empty());

    fixture.service.reset();
    auto created = KaffeeService::create(fixture.store, fixture.settings_store, fixture.platform);
    REQUIRE(static_cast<bool>(created));
    fixture.service = std::move(*created.value);
    CHECK_FALSE(fixture.service->must_change_admin_password());
    CHECK(fixture.service->setup_password().empty());
    CHECK_FALSE(static_cast<bool>(fixture.service->login("admin", kPassword, "10.0.0.10")));
    CHECK(static_cast<bool>(fixture.service->login("admin", "changed-password-1", "10.0.0.11")));
}

TEST_CASE("upgrade without a stored setup password rotates admin credentials") {
    TempDir dir;
    FileEventStore store((dir.path / "events").string());
    MemorySettingsStore settings_store;
    FakePlatform platform;
    configure_platform(platform);

    Settings seeded;
    seeded.admin.username = "admin";
    seeded.admin.salt = "00112233445566778899aabbccddeeff";
    seeded.admin.iterations = 1000;
    seeded.admin.password_hash =
        pbkdf2_sha256_hex(kPassword, seeded.admin.salt, seeded.admin.iterations);
    REQUIRE(static_cast<bool>(settings_store.save(seeded)));

    auto created = KaffeeService::create(store, settings_store, platform);
    REQUIRE(static_cast<bool>(created));
    auto service = std::move(*created.value);
    CHECK(service->must_change_admin_password());
    CHECK(service->setup_password().size() == 16);
    CHECK_FALSE(static_cast<bool>(service->login("admin", kPassword, "10.0.0.12")));
    CHECK(static_cast<bool>(service->login("admin", service->setup_password(), "10.0.0.13")));
}

TEST_CASE("login upgrades old PBKDF2 iteration counts") {
    TempDir dir;
    FileEventStore store((dir.path / "events").string());
    MemorySettingsStore settings_store;
    FakePlatform platform;
    configure_platform(platform);

    Settings seeded;
    seeded.admin.username = "admin";
    seeded.admin.salt = "00112233445566778899aabbccddeeff";
    seeded.admin.iterations = 1000;
    seeded.admin.password_hash =
        pbkdf2_sha256_hex(kPassword, seeded.admin.salt, seeded.admin.iterations);
    REQUIRE(static_cast<bool>(settings_store.save(seeded)));
    REQUIRE(static_cast<bool>(settings_store.save_setup_password(kPassword)));

    auto created = KaffeeService::create(store, settings_store, platform);
    REQUIRE(static_cast<bool>(created));
    auto service = std::move(*created.value);
    CHECK(settings_store.stored().admin.iterations == 1000);
    REQUIRE(static_cast<bool>(service->login("admin", kPassword, "10.0.0.14")));
    CHECK(settings_store.stored().admin.iterations == 10000);
    CHECK(static_cast<bool>(service->login("admin", kPassword, "10.0.0.15")));
}

TEST_CASE("sessions, csrf and monotonic expiry") {
    ServiceFixture fixture(true);
    auto session = fixture.service->login("admin", kPassword, "10.0.0.5");
    REQUIRE(static_cast<bool>(session));
    const std::string token = session.value->token;

    auto validated = fixture.service->validate_session(token);
    REQUIRE(static_cast<bool>(validated));
    CHECK(validated.value->csrf_token == session.value->csrf_token);
    CHECK(fixture.service->validate_csrf(token, session.value->csrf_token));
    CHECK_FALSE(fixture.service->validate_csrf(token, token));
    CHECK_FALSE(fixture.service->validate_csrf(token, "not-the-token"));
    CHECK_FALSE(fixture.service->validate_csrf("unknown", session.value->csrf_token));

    REQUIRE(static_cast<bool>(fixture.service->logout(token)));
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(token)));

    // Idle timeout (30 min), measured on the monotonic clock.
    auto idle = fixture.service->login("admin", kPassword, "10.0.0.6");
    REQUIRE(static_cast<bool>(idle));
    fixture.advance(30 * 60 + 1);
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(idle.value->token)));

    // A backwards wall clock must not extend the session.
    auto backwards = fixture.service->login("admin", kPassword, "10.0.0.7");
    REQUIRE(static_cast<bool>(backwards));
    fixture.platform.time.now -= 7200;
    fixture.platform.stats_.uptime_s += 30 * 60 + 1;
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(backwards.value->token)));

    // Absolute timeout (8 h), with activity every 29 min.
    auto absolute = fixture.service->login("admin", kPassword, "10.0.0.8");
    REQUIRE(static_cast<bool>(absolute));
    const std::string absolute_token = absolute.value->token;
    for (int i = 0; i < 16; ++i) {
        fixture.advance(29 * 60);
        REQUIRE(static_cast<bool>(fixture.service->validate_session(absolute_token)));
    }
    fixture.advance(29 * 60);
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(absolute_token)));
}

TEST_CASE("login rate limit is fixed-window, backs off and audits lockout") {
    ServiceFixture fixture(true);
    const std::string ip = "192.168.4.20";
    for (int i = 0; i < 5; ++i) {
        auto failed = fixture.service->login("admin", "wrong", ip);
        REQUIRE_FALSE(static_cast<bool>(failed));
        CHECK(failed.error->code == "auth");
    }

    bool saw_lockout_audit = false;
    for (const Event& event : fixture.service->events()) {
        if (event.type == EventType::Audit && event.note == "admin_login_locked")
            saw_lockout_audit = true;
    }
    CHECK(saw_lockout_audit);

    auto blocked = fixture.service->login("admin", kPassword, ip);
    REQUIRE_FALSE(static_cast<bool>(blocked));
    CHECK(blocked.error->code == "rate_limit");

    // The wall clock jumping ahead does not release the block; uptime does.
    fixture.platform.time.now += 3600;
    CHECK_FALSE(static_cast<bool>(fixture.service->login("admin", kPassword, ip)));
    fixture.advance(6);
    REQUIRE(static_cast<bool>(fixture.service->login("admin", kPassword, ip)));

    // A successful login clears the failure counter.
    for (int i = 0; i < 4; ++i) {
        CHECK_FALSE(static_cast<bool>(fixture.service->login("admin", "wrong", ip)));
    }
    CHECK(static_cast<bool>(fixture.service->login("admin", kPassword, ip)));
}

TEST_CASE("admin password change persists, is audited and idempotent") {
    ServiceFixture fixture(true);
    auto bad = fixture.service->change_admin_password("wrong", "another-password-1", "chg-0");
    REQUIRE_FALSE(static_cast<bool>(bad));
    CHECK(bad.error->code == "auth");

    auto changed = fixture.service->change_admin_password(kPassword, "another-password-1", "chg-1");
    REQUIRE(static_cast<bool>(changed));
    CHECK_FALSE(fixture.service->must_change_admin_password());
    CHECK(fixture.service->setup_password().empty());
    CHECK_FALSE(static_cast<bool>(fixture.service->login("admin", kPassword, "10.0.1.1")));
    CHECK(static_cast<bool>(fixture.service->login("admin", "another-password-1", "10.0.1.2")));

    const size_t events_after_change = fixture.service->events().size();
    // Retrying the same request id must not verify the now-stale old password.
    CHECK(static_cast<bool>(
        fixture.service->change_admin_password(kPassword, "another-password-1", "chg-1")));
    CHECK(fixture.service->events().size() == events_after_change);

    const auto events = fixture.service->events();
    REQUIRE(!events.empty());
    CHECK(events.back().type == EventType::Audit);
    CHECK(events.back().note == "admin_password_changed");
}

TEST_CASE("new users receive random avatars unless the admin explicitly chooses one") {
    ServiceFixture fixture(true);
    const std::string first = fixture.user("Anna", "random-1");
    auto anna = fixture.service->find_user(first);
    REQUIRE(anna.has_value());
    CHECK(anna->avatar_id >= 1);
    CHECK(anna->avatar_id <= kMaxUserAvatarId);

    // A retried create must not reroll or create a second user.
    const std::string retry = fixture.user("Anna", "random-1");
    CHECK(first == retry);
    CHECK(fixture.service->find_user(first)->avatar_id == anna->avatar_id);
    CHECK(fixture.service->users().size() == 1);

    const std::string second = fixture.user("Ben", "random-2");
    auto ben = fixture.service->find_user(second);
    REQUIRE(ben.has_value());
    CHECK(ben->avatar_id >= 1);
    CHECK(ben->avatar_id <= kMaxUserAvatarId);
    CHECK(ben->avatar_id != anna->avatar_id);

    const auto initials = fixture.service->create_user("Clara", "#D4A373", "manual-0", 0);
    REQUIRE(static_cast<bool>(initials));
    CHECK(fixture.service->find_user(initials.value->user_id)->avatar_id == 0);

    const auto manual = fixture.service->create_user("David", "#D4A373", "manual-24", 24);
    REQUIRE(static_cast<bool>(manual));
    CHECK(fixture.service->find_user(manual.value->user_id)->avatar_id == 24);
    fixture.service.reset();
    auto loaded = KaffeeService::create(fixture.store, fixture.settings_store, fixture.platform);
    REQUIRE(static_cast<bool>(loaded));
    fixture.service = std::move(*loaded.value);
    CHECK(fixture.service->find_user(first)->avatar_id == anna->avatar_id);
    CHECK(fixture.service->find_user(second)->avatar_id == ben->avatar_id);
    CHECK(fixture.service->find_user(initials.value->user_id)->avatar_id == 0);
    CHECK(fixture.service->find_user(manual.value->user_id)->avatar_id == 24);
}

TEST_CASE("mutations are idempotent across validation and state checks") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada", "req-user");
    const std::string duplicate_user = fixture.user("Ada", "req-user");
    CHECK(user_id == duplicate_user);
    CHECK(fixture.service->users().size() == 1);

    const std::string product_id = fixture.product("Kaffee", 60, "req-product");
    auto sale = fixture.service->purchase(user_id, product_id, "req-buy");
    REQUIRE(static_cast<bool>(sale));
    auto retry = fixture.service->purchase(user_id, product_id, "req-buy");
    REQUIRE(static_cast<bool>(retry));
    CHECK(retry.value->id == sale.value->id);
    CHECK(fixture.service->balance(user_id) == -60);

    auto voided = fixture.service->void_event(sale.value->id, "mistake", "req-void");
    REQUIRE(static_cast<bool>(voided));
    auto void_retry = fixture.service->void_event(sale.value->id, "different", "req-void");
    REQUIRE(static_cast<bool>(void_retry));
    CHECK(void_retry.value->id == voided.value->id);

    auto sale2 = fixture.service->purchase(user_id, product_id, "req-buy-2");
    REQUIRE(static_cast<bool>(sale2));
    auto undo = fixture.service->undo_booking(sale2.value->id, "req-undo");
    REQUIRE(static_cast<bool>(undo));
    auto undo_retry = fixture.service->undo_booking(sale2.value->id, "req-undo");
    REQUIRE(static_cast<bool>(undo_retry));
    CHECK(undo_retry.value->id == undo.value->id);
    CHECK(undo.value->actor == "ui");
    CHECK(undo.value->type == EventType::Void);

    auto archived = fixture.service->archive_user(user_id, "req-archive");
    REQUIRE(static_cast<bool>(archived));
    auto archive_retry = fixture.service->archive_user(user_id, "req-archive");
    REQUIRE(static_cast<bool>(archive_retry));
    CHECK(archive_retry.value->id == archived.value->id);

    auto deleted = fixture.service->delete_product(product_id, "req-delete");
    REQUIRE(static_cast<bool>(deleted));
    auto delete_retry = fixture.service->delete_product(product_id, "req-delete");
    REQUIRE(static_cast<bool>(delete_retry));
    CHECK(delete_retry.value->id == deleted.value->id);
}

TEST_CASE("retrying a booking still returns the original after the debt limit changes") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 60);

    Settings settings = fixture.service->settings();
    settings.app.max_open_debt_cents = 100;
    REQUIRE(static_cast<bool>(fixture.service->save_settings(settings, "limit-high")));

    auto sale = fixture.service->purchase(user_id, product_id, "req-debt");
    REQUIRE(static_cast<bool>(sale));

    settings.app.max_open_debt_cents = 10;
    REQUIRE(static_cast<bool>(fixture.service->save_settings(settings, "limit-low")));

    // A fresh request would be rejected, but the retry is idempotent.
    auto retry = fixture.service->purchase(user_id, product_id, "req-debt");
    REQUIRE(static_cast<bool>(retry));
    CHECK(retry.value->id == sale.value->id);
    CHECK(fixture.service->balance(user_id) == -60);

    auto rejected = fixture.service->purchase(user_id, product_id, "req-debt-new");
    REQUIRE_FALSE(static_cast<bool>(rejected));
    CHECK(rejected.error->code == "debt");
}

TEST_CASE("settings never expose secrets and keep them when omitted") {
    ServiceFixture fixture(true);
    const Settings initial = fixture.settings_store.stored();
    REQUIRE_FALSE(initial.network.ap_password.empty());

    const Settings visible = fixture.service->settings();
    CHECK(visible.network.ap_password.empty());
    CHECK(visible.network.sta_password.empty());
    CHECK(visible.admin.password_hash.empty());
    CHECK(visible.admin.salt.empty());

    Settings input = visible;
    input.network.ap_ssid = "Kaffeekasse-Test";
    input.app.guest_enabled = false;
    REQUIRE(static_cast<bool>(fixture.service->save_settings(input, "set-1")));

    const Settings stored = fixture.settings_store.stored();
    CHECK(stored.network.ap_password == initial.network.ap_password);
    CHECK(stored.admin.password_hash == initial.admin.password_hash);
    CHECK_FALSE(stored.app.guest_enabled);
    CHECK(stored.network.ap_ssid == "Kaffeekasse-Test");

    const auto events = fixture.service->events();
    REQUIRE(!events.empty());
    CHECK(events.back().note == "settings_updated");
    CHECK(events.back().meta_json.find("ap-secret") == std::string::npos);
    CHECK(events.back().meta_json.find("password_hash") == std::string::npos);

    // Same request id returns success without applying the changed payload.
    input.network.ap_ssid = "Ignored-Retry";
    input.network.ap_password = "brand-new-secret";
    REQUIRE(static_cast<bool>(fixture.service->save_settings(input, "set-1")));
    CHECK(fixture.settings_store.stored().network.ap_ssid == "Kaffeekasse-Test");
    CHECK(fixture.settings_store.stored().network.ap_password == initial.network.ap_password);

    REQUIRE(static_cast<bool>(fixture.service->save_settings(input, "set-2")));
    CHECK(fixture.settings_store.stored().network.ap_password == "brand-new-secret");
}

TEST_CASE("display settings are validated and delegated") {
    ServiceFixture fixture(true);
    DisplaySettings display;
    display.rotation = 90;
    display.brightness_active = 70;
    REQUIRE(static_cast<bool>(fixture.service->apply_display_settings(display)));
    CHECK(fixture.platform.display.rotation == 90);
    CHECK(fixture.service->display_settings().rotation == 90);

    display.rotation = 45;
    CHECK_FALSE(static_cast<bool>(fixture.service->apply_display_settings(display)));
}

TEST_CASE("time quality follows the platform and browser sync") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 50);

    fixture.platform.time.source = TimeSource::Restored;
    auto restored = fixture.service->purchase(user_id, product_id, "t-1");
    REQUIRE(static_cast<bool>(restored));
    CHECK(restored.value->time_source == TimeSource::Restored);
    CHECK(restored.value->ts == kNow);

    REQUIRE(static_cast<bool>(fixture.service->sync_time(kNow + 120, "browser")));
    CHECK(fixture.platform.synced_epoch == kNow + 120);
    CHECK(fixture.service->time_info().source == TimeSource::Synced);

    auto synced = fixture.service->purchase(user_id, product_id, "t-2");
    REQUIRE(static_cast<bool>(synced));
    CHECK(synced.value->time_source == TimeSource::Synced);
    CHECK(synced.value->ts == kNow + 120);
}

TEST_CASE("ap delegation and timeout clear sessions") {
    ServiceFixture fixture(true);
    auto session = fixture.service->login("admin", kPassword, "10.0.0.9");
    REQUIRE(static_cast<bool>(session));

    REQUIRE(static_cast<bool>(fixture.service->start_ap()));
    CHECK(fixture.platform.ap_starts == 1);
    REQUIRE(static_cast<bool>(fixture.service->extend_ap(60)));
    CHECK(fixture.platform.last_extend == 60);

    // AP auto-off timer fires outside the service.
    fixture.platform.simulate_ap_timeout();
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(session.value->token)));

    auto second = fixture.service->login("admin", kPassword, "10.0.0.10");
    REQUIRE(static_cast<bool>(second));
    REQUIRE(static_cast<bool>(fixture.service->stop_ap()));
    CHECK(fixture.platform.ap_stops == 1);
    // Session clearing is deferred to the listener callback, never done on the
    // httpd or serial task that requested the stop.
    CHECK(static_cast<bool>(fixture.service->validate_session(second.value->token)));
    fixture.platform.simulate_ap_timeout();
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(second.value->token)));
}

TEST_CASE("change callback is queued off the mutating thread") {
    ServiceFixture fixture(true);
    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    bool called = false;
    std::thread::id callback_thread;
    const std::thread::id caller_thread = std::this_thread::get_id();

    fixture.service->set_change_callback([&] {
        std::lock_guard<std::mutex> lock(callback_mutex);
        called = true;
        callback_thread = std::this_thread::get_id();
        callback_cv.notify_all();
    });

    fixture.user("Ada");
    {
        std::unique_lock<std::mutex> lock(callback_mutex);
        REQUIRE(callback_cv.wait_for(lock, std::chrono::seconds(2), [&] { return called; }));
    }
    CHECK(callback_thread != caller_thread);
    CHECK(fixture.platform.notifications >= 1);

    called = false;
    fixture.service->refresh();
    {
        std::unique_lock<std::mutex> lock(callback_mutex);
        REQUIRE(callback_cv.wait_for(lock, std::chrono::seconds(2), [&] { return called; }));
    }
}

TEST_CASE("backup excludes secrets and restores state") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 50);
    REQUIRE(static_cast<bool>(fixture.service->topup(user_id, 1000, "cash", "b-1")));
    REQUIRE(static_cast<bool>(fixture.service->purchase(user_id, product_id, "b-2")));
    REQUIRE(static_cast<bool>(
        fixture.service->add_expense("Maschine", "Entkalkung", "Shop", 300, "", "b-3")));

    auto backup = fixture.service->export_backup();
    REQUIRE(static_cast<bool>(backup));
    CHECK(backup.value->find("ap-secret") == std::string::npos);
    CHECK(backup.value->find("password_hash") == std::string::npos);
    const std::string setup_password = fixture.service->setup_password();
    REQUIRE_FALSE(setup_password.empty());
    CHECK(backup.value->find(setup_password) == std::string::npos);
    CHECK(backup.value->find("setup_password") == std::string::npos);

    const auto parsed = nlohmann::json::parse(*backup.value);
    CHECK(parsed["format"] == "kaffeekasse-backup");
    CHECK(parsed["version"] == 1);
    CHECK_FALSE(parsed["settings"].contains("admin"));
    CHECK_FALSE(parsed["settings"]["network"].contains("ap_password"));
    CHECK_FALSE(parsed["settings"]["network"].contains("sta_password"));
    CHECK(parsed["events"].size() == fixture.service->events().size());
    CHECK(fixture.service->stats().last_backup_at == kNow);

    TempDir other_dir;
    FileEventStore other_store((other_dir.path / "events").string());
    MemorySettingsStore other_settings;
    FakePlatform other_platform;
    configure_platform(other_platform);
    // Model a second physical device with its own RNG state: otherwise the
    // deterministic test RNG can recycle an imported event ID for the restore audit.
    other_platform.rng = 0x8ac723d52b1f904eull;
    other_platform.time.now = kNow + 500;
    auto created = KaffeeService::create(other_store, other_settings, other_platform);
    REQUIRE(static_cast<bool>(created));
    std::unique_ptr<KaffeeService> other = std::move(*created.value);

    auto restored = other->import_backup(*backup.value, "restore-1");
    const std::string restore_diagnostic =
        restored.error ? restored.error->message : "backup restore succeeded";
    INFO(restore_diagnostic);
    REQUIRE(static_cast<bool>(restored));
    CHECK(other->users().size() == fixture.service->users().size());
    CHECK(other->products().size() == fixture.service->products().size());
    CHECK(other->events().size() == fixture.service->events().size() + 1);  // restore audit
    CHECK(other->balance(user_id) == fixture.service->balance(user_id));
    CHECK(other->summary().cash_expected == fixture.service->summary().cash_expected);
    CHECK(other->summary().debts == fixture.service->summary().debts);
    CHECK(other->summary().expenses == fixture.service->summary().expenses);
    CHECK(other->events().back().note == "backup_restored");

    // The restore target keeps its own generated secrets.
    CHECK_FALSE(other_settings.stored().network.ap_password.empty());
    CHECK_FALSE(other_settings.stored().admin.password_hash.empty());
    CHECK_FALSE(other_settings.stored().admin.salt.empty());

    const size_t before = other->events().size();
    REQUIRE(static_cast<bool>(other->import_backup(*backup.value, "restore-1")));
    CHECK(other->events().size() == before);
}

TEST_CASE("service reloads the ledger and admin credentials from storage") {
    TempDir dir;
    FileEventStore store((dir.path / "events").string());
    MemorySettingsStore settings_store;
    FakePlatform platform;
    configure_platform(platform);

    {
        auto created = KaffeeService::create(store, settings_store, platform);
        REQUIRE(static_cast<bool>(created));
        auto service = std::move(*created.value);
        must_event(service->create_user("Ada", "#fff", "reload-1"));
        REQUIRE(static_cast<bool>(service->login("admin", service->setup_password(), "ip")));
        REQUIRE(static_cast<bool>(service->change_admin_password(service->setup_password(),
                                                                 "reloaded-password", "reload-2")));
    }

    auto created = KaffeeService::create(store, settings_store, platform);
    REQUIRE(static_cast<bool>(created));
    auto service = std::move(*created.value);
    CHECK(service->users().size() == 1);
    CHECK_FALSE(service->must_change_admin_password());
    CHECK(static_cast<bool>(service->login("admin", "reloaded-password", "ip-2")));
}

TEST_CASE("settings load failure fails init without overwriting credentials") {
    TempDir dir;
    FileEventStore store((dir.path / "events").string());
    FailingSettingsStore settings;
    FakePlatform platform;
    configure_platform(platform);
    settings.fail_load = true;

    auto created = KaffeeService::create(store, settings, platform);
    REQUIRE_FALSE(static_cast<bool>(created));
    CHECK(created.error->code == "settings");
    CHECK(settings.stored().admin.password_hash.empty());

    settings.fail_load = false;
    auto retry = KaffeeService::create(store, settings, platform);
    REQUIRE(static_cast<bool>(retry));
}

TEST_CASE("failed settings save during restore leaves ledger and settings untouched") {
    TempDir dir;
    FileEventStore store((dir.path / "events").string());
    FailingSettingsStore settings;
    FakePlatform platform;
    configure_platform(platform);

    auto created = KaffeeService::create(store, settings, platform);
    REQUIRE(static_cast<bool>(created));
    auto service = std::move(*created.value);
    const std::string user_id = must_event(service->create_user("Ada", "#fff", "r-1")).user_id;

    auto backup = service->export_backup();
    REQUIRE(static_cast<bool>(backup));
    auto json = nlohmann::json::parse(*backup.value);
    json["settings"]["app"]["guest_enabled"] = false;

    settings.fail_save = true;
    auto imported = service->import_backup(json.dump(), "restore-fail");
    REQUIRE_FALSE(static_cast<bool>(imported));
    CHECK(imported.error->code == "settings");
    CHECK(service->users().size() == 1);
    CHECK(service->events().size() == 1);
    CHECK(service->find_user(user_id).has_value());

    settings.fail_save = false;
    CHECK(static_cast<bool>(service->import_backup(json.dump(), "restore-ok")));
}

TEST_CASE("accessor snapshots are stable copies") {
    ServiceFixture fixture(true);
    const auto before = fixture.service->users();
    CHECK(before.empty());

    fixture.user("Ada");
    CHECK(before.empty());
    CHECK(fixture.service->users().size() == 1);

    // Readers and writers can run concurrently without dangling references.
    std::atomic<bool> stop{false};
    std::thread reader([&] {
        while (!stop.load()) {
            const auto users = fixture.service->users();
            const auto events = fixture.service->events();
            (void)users;
            (void)events;
        }
    });
    for (int i = 0; i < 50; ++i) {
        fixture.user("Member " + std::to_string(i), "concurrent-" + std::to_string(i));
    }
    stop = true;
    reader.join();
    CHECK(fixture.service->users().size() == 51);
}

TEST_CASE("ledger snapshot is a stable report view") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 60);
    REQUIRE(static_cast<bool>(fixture.service->purchase(user_id, product_id, "snap-buy")));

    const std::shared_ptr<const Ledger> snapshot = fixture.service->ledger_snapshot();
    REQUIRE(snapshot != nullptr);
    const size_t snapshot_events = snapshot->events().size();
    CHECK(snapshot->users().size() == 1);
    CHECK(snapshot->balance(user_id) == -60);

    fixture.user("Ben");
    CHECK(snapshot->events().size() == snapshot_events);
    CHECK(fixture.service->ledger_snapshot()->events().size() == snapshot_events + 1);
}

TEST_CASE("settings save applies network and time to the platform") {
    ServiceFixture fixture(true);
    Settings next = fixture.service->settings();
    next.network.ap_ssid = "Kaffeekasse-Test";
    next.network.hostname = "kaffee-test";
    next.time.timezone = "Europe/Berlin";
    next.time.sntp_enabled = false;

    REQUIRE(static_cast<bool>(fixture.service->save_settings(next, "settings-net")));
    CHECK(fixture.platform.network_applies == 1);
    CHECK(fixture.platform.time_applies == 1);
    CHECK(fixture.platform.network_settings.ap_ssid == "Kaffeekasse-Test");
    CHECK(fixture.platform.network_settings.hostname == "kaffee-test");
    CHECK_FALSE(fixture.platform.network_settings.ap_password.empty());
    CHECK(fixture.platform.time_settings.timezone == "Europe/Berlin");
}

TEST_CASE("failed network apply rolls back settings") {
    ServiceFixture fixture(true);
    const Settings before = fixture.service->settings();
    Settings next = before;
    next.network.ap_ssid = "Kaffeekasse-Should-Not-Persist";
    fixture.platform.fail_network_apply = true;

    auto saved = fixture.service->save_settings(next, "settings-rollback");
    REQUIRE_FALSE(static_cast<bool>(saved));
    CHECK(fixture.service->settings().network.ap_ssid == before.network.ap_ssid);
    CHECK(fixture.settings_store.stored().network.ap_ssid == before.network.ap_ssid);
    CHECK(fixture.platform.network_settings.ap_ssid == before.network.ap_ssid);
}

TEST_CASE("service stats include storage, backup and drinks") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 50);
    REQUIRE(static_cast<bool>(fixture.service->purchase(user_id, product_id, "stats-1")));
    REQUIRE(static_cast<bool>(fixture.service->purchase(user_id, product_id, "stats-2")));

    const SystemStats before = fixture.service->stats();
    CHECK(before.storage.mounted);
    CHECK(before.storage.total_bytes > 0);
    CHECK(before.drinks_today == 2);
    CHECK(before.last_backup_at == 0);

    REQUIRE(static_cast<bool>(fixture.service->export_backup()));
    const SystemStats after = fixture.service->stats();
    CHECK(after.last_backup_at == kNow);
    CHECK(after.storage.used_bytes > 0);
    CHECK(after.drinks_today == 2);
}

TEST_CASE("service stats polling is cached and does not leak fds") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 50);
    REQUIRE(static_cast<bool>(fixture.service->purchase(user_id, product_id, "stats-cache")));

    const size_t fds_before = open_fd_count();
    const SystemStats first = fixture.service->stats();
    for (int i = 0; i < 5000; ++i) {
        const SystemStats stats = fixture.service->stats();
        REQUIRE(stats.storage.mounted);
    }
    const SystemStats last = fixture.service->stats();
    const size_t fds_after = open_fd_count();
    CHECK(last.storage.used_bytes == first.storage.used_bytes);
    CHECK(last.drinks_today == first.drinks_today);
    CHECK(last.last_backup_at == first.last_backup_at);
    CHECK(fds_after <= fds_before + 4);
}

TEST_CASE("drinks today excludes voided purchases") {
    ServiceFixture fixture(true);
    const std::string user_id = fixture.user("Ada");
    const std::string product_id = fixture.product("Kaffee", 50);

    auto sale = fixture.service->purchase(user_id, product_id, "stats-void");
    REQUIRE(static_cast<bool>(sale));
    CHECK(fixture.service->stats().drinks_today == 1);

    REQUIRE(static_cast<bool>(
        fixture.service->void_event(sale.value->id, "mistake", "stats-void-rev")));
    CHECK(fixture.service->stats().drinks_today == 0);
}

TEST_CASE("factory reset clears storage, settings and sessions") {
    ServiceFixture fixture(true);
    fixture.user("Ada");
    fixture.product("Kaffee", 60);
    auto session = fixture.service->login("admin", kPassword, "10.0.10.1");
    REQUIRE(static_cast<bool>(session));

    REQUIRE(static_cast<bool>(fixture.service->factory_reset()));
    CHECK(fixture.service->users().empty());
    CHECK(fixture.service->events().empty());
    CHECK(fixture.service->summary().cash_expected == 0);
    CHECK_FALSE(static_cast<bool>(fixture.service->validate_session(session.value->token)));
    CHECK(fixture.service->must_change_admin_password());
    CHECK(fixture.service->settings().network.ap_ssid.empty());
    CHECK(fixture.settings_store.stored().admin.password_hash.empty());
    CHECK(fixture.store.info().event_count == 0);
}

TEST_CASE("journal compaction is scheduled by the event threshold") {
    ServiceFixture fixture(true);
    for (int i = 0; i < 300; ++i) {
        fixture.user("Member " + std::to_string(i), "member-" + std::to_string(i));
    }
    CHECK(fixture.service->users().size() == 300);
    const StorageInfo info = fixture.store.info();
    CHECK(info.journal_events < 256);
    const bool has_snapshot = fs::exists(fixture.store.snapshot_path('a')) ||
                              fs::exists(fixture.store.snapshot_path('b'));
    CHECK(has_snapshot);
}
