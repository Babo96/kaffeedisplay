#include <doctest.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "kaffee/backup.hpp"
#include "kaffee/file_store.hpp"
#include "kaffee/json.hpp"
#include "kaffee/reports.hpp"
#include "kaffee/sd_backup.hpp"
#include "kaffee/service.hpp"

using namespace kaffee;
namespace fs = std::filesystem;
using kaffee::sd::BackupData;
using kaffee::sd::execute_plan;
using kaffee::sd::plan_for;
using kaffee::sd::SdPlan;
using kaffee::sd::SdRunState;

namespace {

Timestamp at(int year, int month, int day, int hour, int minute, int second = 0) {
    reports::Civil civil;
    civil.year = year;
    civil.month = month;
    civil.day = day;
    civil.hour = hour;
    civil.minute = minute;
    civil.second = second;
    return reports::from_berlin(civil);
}

struct FakeVolume final : sd::IVolume {
    bool mounted = false;
    bool fail_mount = false;
    bool fail_write = false;
    int mounts = 0;
    int unmounts = 0;
    int stream_calls = 0;
    std::vector<std::string> order;
    std::map<std::string, std::string> files;

    Result<Unit> mount() override {
        ++mounts;
        if (fail_mount) return Result<Unit>::fail("sd", "mount: no card");
        mounted = true;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> unmount() override {
        ++unmounts;
        mounted = false;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> write_file(const std::string& name, std::string_view data) override {
        order.push_back(name);
        if (fail_write) return Result<Unit>::fail("sd", "write failed: " + name);
        files[name] = std::string(data);
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> write_stream(const std::string& name, const StreamProducer& produce) override {
        ++stream_calls;
        order.push_back(name);
        if (fail_write) return Result<Unit>::fail("sd", "write failed: " + name);
        reports::Sink sink{[&](std::string_view chunk) {
            files[name].append(chunk.data(), chunk.size());
            return true;
        }};
        return produce(sink);
    }
};

struct FakeStateStore final : sd::IStateStore {
    SdRunState state;
    int saves = 0;

    Result<SdRunState> load_last_success() override {
        return Result<SdRunState>::ok(state);
    }
    Result<Unit> save_last_success(const SdRunState& value) override {
        ++saves;
        state = value;
        return Result<Unit>::ok(Unit{});
    }
};

std::shared_ptr<const Ledger> empty_ledger() {
    return std::make_shared<Ledger>();
}

class StubPlatform final : public IPlatform {
  public:
    TimeInfo time;
    uint64_t rng = 0x123456789abcdef0ull;

    SystemStats stats() const override {
        return SystemStats{};
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
        (void)source;
        time.now = epoch_seconds;
        time.source = TimeSource::Synced;
        time.trusted = true;
        return Result<Unit>::ok(Unit{});
    }
    NetworkStatus network_status() const override {
        return NetworkStatus{};
    }
    Result<Unit> start_ap() override {
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> stop_ap() override {
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> extend_ap(int seconds) override {
        (void)seconds;
        return Result<Unit>::ok(Unit{});
    }
    DisplaySettings display_settings() const override {
        return DisplaySettings{};
    }
    Result<Unit> apply_display_settings(const DisplaySettings& settings) override {
        (void)settings;
        return Result<Unit>::ok(Unit{});
    }
    Result<Unit> show_display_test() override {
        return Result<Unit>::ok(Unit{});
    }
    void notify_ledger_changed() override {}
};

struct TempDir {
    fs::path path;
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() /
               ("kaffee-sd-" + std::to_string(::getpid()) + "-" + std::to_string(stamp));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("sd plan: fresh trusted boot after 00:08 writes previous, current and DB") {
    const Timestamp now = at(2026, 11, 25, 10, 0);
    const SdPlan plan = plan_for(now, SdRunState{}, TimeSource::Synced, true);
    CHECK(plan.write_db);
    CHECK(plan.write_previous_csv);
    CHECK(plan.write_current_csv);
    CHECK(plan.due());
}

TEST_CASE("sd plan: steady state after a successful run is idle") {
    const Timestamp run = at(2026, 11, 25, 0, 8);
    SdRunState state;
    state.last_db_success = run;
    state.last_csv_success = run;

    CHECK_FALSE(plan_for(at(2026, 11, 25, 10, 0), state, TimeSource::Synced, true).due());
    CHECK_FALSE(plan_for(at(2026, 11, 25, 23, 59), state, TimeSource::Synced, true).due());
}

TEST_CASE("sd plan: next day after 00:08 runs DB and current month CSV") {
    SdRunState state;
    state.last_db_success = at(2026, 11, 24, 0, 8);
    state.last_csv_success = at(2026, 11, 24, 0, 8);

    const SdPlan plan = plan_for(at(2026, 11, 25, 0, 8), state, TimeSource::Synced, true);
    CHECK(plan.write_db);
    CHECK(plan.write_current_csv);
    CHECK_FALSE(plan.write_previous_csv);
}

TEST_CASE("sd plan: before 00:08 the previous day's run still counts") {
    SdRunState state;
    state.last_db_success = at(2026, 11, 24, 0, 8);
    state.last_csv_success = at(2026, 11, 24, 0, 8);

    const SdPlan plan = plan_for(at(2026, 11, 25, 0, 5), state, TimeSource::Synced, true);
    CHECK_FALSE(plan.due());

    // Two days without a run: the missed day is caught up before 00:08.
    state.last_db_success = at(2026, 11, 23, 0, 8);
    state.last_csv_success = at(2026, 11, 23, 0, 8);
    const SdPlan catch_up = plan_for(at(2026, 11, 25, 0, 5), state, TimeSource::Synced, true);
    CHECK(catch_up.write_db);
    CHECK(catch_up.write_current_csv);
    CHECK_FALSE(catch_up.write_previous_csv);
}

TEST_CASE("sd plan: month boundary finalises the previous month first") {
    SdRunState state;
    state.last_db_success = at(2026, 11, 30, 0, 8);
    state.last_csv_success = at(2026, 11, 30, 0, 8);

    const Timestamp now = at(2026, 12, 1, 0, 8);
    const SdPlan plan = plan_for(now, state, TimeSource::Synced, true);
    CHECK(plan.write_previous_csv);
    CHECK(plan.write_current_csv);
    CHECK(plan.write_db);
    CHECK(sd::csv_path(now, true) == "kaffeekasse_2026-11_transactions.csv");
    CHECK(sd::csv_path(now, false) == "kaffeekasse_2026-12_transactions.csv");
}

TEST_CASE("sd plan: before 00:08 on the 1st only the previous month is finalised") {
    SdRunState state;
    state.last_db_success = at(2026, 11, 30, 0, 8);
    state.last_csv_success = at(2026, 11, 30, 0, 8);

    const SdPlan plan = plan_for(at(2026, 12, 1, 0, 5), state, TimeSource::Synced, true);
    CHECK(plan.write_previous_csv);
    CHECK_FALSE(plan.write_current_csv);
    CHECK_FALSE(plan.write_db);
}

TEST_CASE("sd plan: an untrusted clock never writes CSV") {
    SdRunState state;
    state.last_db_success = at(2026, 11, 24, 0, 8);
    state.last_csv_success = at(2026, 11, 24, 0, 8);

    const SdPlan restored = plan_for(at(2026, 11, 25, 0, 8), state, TimeSource::Restored, true);
    CHECK(restored.write_db);
    CHECK_FALSE(restored.write_previous_csv);
    CHECK_FALSE(restored.write_current_csv);

    const SdPlan unset = plan_for(0, SdRunState{}, TimeSource::Unset, true);
    CHECK(unset.write_db);
    CHECK_FALSE(unset.write_current_csv);

    SdRunState ran_without_clock;
    ran_without_clock.last_db_success = 1;
    CHECK_FALSE(plan_for(0, ran_without_clock, TimeSource::Unset, true).due());
}

TEST_CASE("sd plan: a missing card does nothing") {
    const SdPlan plan = plan_for(at(2026, 11, 25, 0, 8), SdRunState{}, TimeSource::Synced, false);
    CHECK_FALSE(plan.due());
}

TEST_CASE("sd execute: no due work does not touch the card") {
    FakeVolume volume;
    FakeStateStore store;
    auto result = execute_plan(at(2026, 11, 25, 10, 0), SdPlan{}, volume, store, SdRunState{}, {});
    REQUIRE(static_cast<bool>(result));
    CHECK(volume.mounts == 0);
    CHECK(volume.order.empty());
}

TEST_CASE("sd execute: mount failure writes nothing and keeps the markers") {
    FakeVolume volume;
    volume.fail_mount = true;
    FakeStateStore store;
    SdPlan plan;
    plan.write_db = true;
    plan.write_current_csv = true;

    BackupData data;
    data.ledger = empty_ledger();
    data.db_json = "{}";

    auto result = execute_plan(at(2026, 11, 25, 0, 8), plan, volume, store, SdRunState{}, data);
    CHECK_FALSE(static_cast<bool>(result));
    CHECK(volume.order.empty());
    CHECK(store.saves == 0);
    CHECK(store.state.last_db_success == 0);
    CHECK(store.state.last_csv_success == 0);
}

TEST_CASE("sd execute: failed write does not advance the marker and is retried") {
    const Timestamp now = at(2026, 11, 25, 0, 8);
    SdPlan plan;
    plan.write_current_csv = true;
    plan.write_db = true;

    FakeVolume volume;
    volume.fail_write = true;
    FakeStateStore store;
    BackupData data;
    data.ledger = empty_ledger();
    data.db_json = "db-payload";

    auto failed = execute_plan(now, plan, volume, store, SdRunState{}, data);
    CHECK_FALSE(static_cast<bool>(failed));
    CHECK(store.state.last_csv_success == 0);
    CHECK(store.state.last_db_success == 0);
    CHECK(volume.unmounts == 1);

    // Same day, a later poll sees the same plan and succeeds.
    CHECK(plan_for(now + 60, store.state, TimeSource::Synced, true).write_current_csv);
    volume.fail_write = false;
    auto retried = execute_plan(now + 60, plan, volume, store, store.state, data);
    REQUIRE(static_cast<bool>(retried));
    CHECK(store.state.last_csv_success == now + 60);
    CHECK(store.state.last_db_success == now + 60);
    CHECK(volume.unmounts == 2);
}

TEST_CASE("sd execute: month boundary writes previous, current, then DB") {
    const Timestamp now = at(2026, 12, 1, 0, 8);
    SdPlan plan;
    plan.write_previous_csv = true;
    plan.write_current_csv = true;
    plan.write_db = true;

    FakeVolume volume;
    FakeStateStore store;
    BackupData data;
    data.ledger = empty_ledger();
    data.db_json = "db-payload";

    auto result = execute_plan(now, plan, volume, store, SdRunState{}, data);
    REQUIRE(static_cast<bool>(result));
    REQUIRE(volume.order.size() == 3);
    CHECK(volume.order[0] == "kaffeekasse_2026-11_transactions.csv");
    CHECK(volume.order[1] == "kaffeekasse_2026-12_transactions.csv");
    CHECK(volume.order[2] == "kaffeekasse-db.json");
    CHECK(volume.stream_calls == 2);
    CHECK(store.state.last_csv_success == now);
    CHECK(store.state.last_db_success == now);

    // The CSV documents are produced through the report sink, not a buffer.
    const std::string& csv = volume.files["kaffeekasse_2026-12_transactions.csv"];
    CHECK(csv.rfind("\xEF\xBB\xBF", 0) == 0);
    CHECK(csv.find("sep=;") != std::string::npos);
    CHECK(volume.files["kaffeekasse-db.json"] == "db-payload");
}

TEST_CASE("sd probe decision: five-minute interval, first probe and clock reset") {
    CHECK(sd::probe_due(100, 0));
    CHECK_FALSE(sd::probe_due(399, 100));
    CHECK(sd::probe_due(400, 100));
    CHECK(sd::probe_due(900, 100));
    // A monotonic clock reset after reboot must not suppress the probe.
    CHECK(sd::probe_due(50, 100));
    CHECK_FALSE(sd::probe_due(0, 0));
}

TEST_CASE("sd status monitor defaults to not detected and publishes copies") {
    sd::SdStatusMonitor monitor;
    sd::SdStatus status = monitor.snapshot();
    CHECK_FALSE(status.card_present);
    CHECK_FALSE(status.backup_ok);
    CHECK(status.last_success == 0);
    CHECK(status.last_error.empty());

    monitor.set_last_success(sd::kNoClockSuccess);
    CHECK(monitor.snapshot().last_success == 0);
    monitor.set_last_success(1234);
    CHECK(monitor.snapshot().last_success == 1234);

    monitor.record_error("mount: no compatible card", false);
    status = monitor.snapshot();
    CHECK_FALSE(status.card_present);
    CHECK_FALSE(status.backup_ok);
    CHECK(status.last_success == 1234);
    CHECK(status.last_error == "mount: no compatible card");

    monitor.record_success(5678);
    status = monitor.snapshot();
    CHECK(status.card_present);
    CHECK(status.backup_ok);
    CHECK(status.last_success == 5678);
    CHECK(status.last_error.empty());

    // A clock-independent DB run must not replace a real timestamp with 1970.
    monitor.record_success(0);
    CHECK(monitor.snapshot().last_success == 5678);
}

TEST_CASE("sd status monitor: probes only refresh presence, not the last backup result") {
    sd::SdStatusMonitor monitor;
    monitor.record_success(4321);
    monitor.record_probe_failure();
    sd::SdStatus status = monitor.snapshot();
    CHECK_FALSE(status.card_present);
    CHECK(status.backup_ok);
    CHECK(status.last_success == 4321);
    CHECK(status.last_error.empty());

    monitor.record_probe_success();
    status = monitor.snapshot();
    CHECK(status.card_present);
    CHECK(status.backup_ok);
    CHECK(status.last_success == 4321);
    CHECK(status.last_error.empty());

    // A failed real run records its error; a later probe must not replace it.
    monitor.record_error("write failed: kaffeekasse-db.json", true);
    monitor.record_probe_failure();
    status = monitor.snapshot();
    CHECK_FALSE(status.card_present);
    CHECK_FALSE(status.backup_ok);
    CHECK(status.last_success == 4321);
    CHECK(status.last_error == "write failed: kaffeekasse-db.json");

    monitor.record_probe_success();
    status = monitor.snapshot();
    CHECK(status.card_present);
    CHECK_FALSE(status.backup_ok);
    CHECK(status.last_error == "write failed: kaffeekasse-db.json");
}

TEST_CASE("sd DB backup matches export_backup modulo created_at and contains no secrets") {
    TempDir dir;
    FileEventStore store((dir.path / "events").string());
    MemorySettingsStore settings_store;
    Settings seeded;
    seeded.network.ap_ssid = "Kaffeekasse-AB12";
    seeded.network.ap_password = "ap-secret";
    seeded.network.sta_ssid = "home";
    seeded.network.sta_password = "sta-secret";
    seeded.admin.password_hash = "deadbeef";
    seeded.admin.salt = "cafe";
    REQUIRE(static_cast<bool>(settings_store.save(seeded)));

    StubPlatform platform;
    platform.time.now = at(2026, 11, 25, 10, 0);
    platform.time.source = TimeSource::Synced;
    platform.time.trusted = true;

    auto created = KaffeeService::create(store, settings_store, platform);
    REQUIRE(static_cast<bool>(created));
    auto service = std::move(*created.value);

    auto user = service->create_user("Ada", "#D4A373", "u-1");
    REQUIRE(static_cast<bool>(user));
    auto product = service->create_product("Kaffee", "Kaffee", "cup", "#D4A373", 50, 0, "p-1");
    REQUIRE(static_cast<bool>(product));
    REQUIRE(static_cast<bool>(service->topup(user.value->user_id, 1000, "cash", "t-1")));
    REQUIRE(static_cast<bool>(
        service->purchase(user.value->user_id, product.value->product_id, "b-1")));

    auto exported = service->export_backup();
    REQUIRE(static_cast<bool>(exported));

    auto ledger = service->ledger_snapshot();
    REQUIRE(ledger != nullptr);
    const std::string sd_json = backup_to_json(*ledger, service->settings(), platform.time.now);

    auto official = nlohmann::json::parse(*exported.value);
    auto written = nlohmann::json::parse(sd_json);
    official.erase("created_at");
    written.erase("created_at");
    CHECK(official == written);

    CHECK(sd_json.find("ap-secret") == std::string::npos);
    CHECK(sd_json.find("sta-secret") == std::string::npos);
    CHECK(sd_json.find("password_hash") == std::string::npos);
    CHECK(sd_json.find("deadbeef") == std::string::npos);

    FakeVolume volume;
    FakeStateStore state_store;
    SdPlan plan;
    plan.write_db = true;
    BackupData data;
    data.db_json = sd_json;
    auto result = execute_plan(platform.time.now, plan, volume, state_store, SdRunState{}, data);
    REQUIRE(static_cast<bool>(result));
    CHECK(volume.files["kaffeekasse-db.json"] == sd_json);
    CHECK(state_store.state.last_db_success == platform.time.now);
}
