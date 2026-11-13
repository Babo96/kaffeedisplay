#include "fake_backend.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace sim {

FakeBackend::FakeBackend() {
    seed();
}

void FakeBackend::seed() {
    const kaffee::Timestamp created = now_ - 86400 * 20;

    // Vary the preset IDs for screenshots; keep one initials-only user as fallback.
    auto add_user = [&](const char* name, const char* color, kaffee::Cents topup, int avatar_id) {
        auto event = ledger_.create_user(name, color, std::string("seed-user-") + name, created,
                                         kaffee::TimeSource::Synced, avatar_id);
        std::string id = event.value ? event.value->user_id : std::string();
        if (topup > 0 && !id.empty()) {
            ledger_.topup(id, topup, "Startguthaben", std::string("seed-topup-") + name,
                          now_ - 86400 * 10, kaffee::TimeSource::Synced);
        }
        return id;
    };

    const std::string anna = add_user("Anna", "#D4A373", 2000, 1);
    const std::string ben = add_user("Ben", "#8FBC8F", 1000, 8);
    const std::string clara = add_user("Clara", "#7FA6D9", 500, 15);
    const std::string david = add_user("David", "#D98F8F", 0, 22);
    const std::string emma = add_user("Emma", "#C9A0DC", 1500, 0);

    auto add_product = [&](const char* name, const char* category, const char* icon,
                           const char* color, kaffee::Cents price, int sort) {
        auto event = ledger_.create_product(name, category, icon, color, price, sort,
                                            std::string("seed-product-") + name, created,
                                            kaffee::TimeSource::Synced);
        return event.value ? event.value->product_id : std::string();
    };

    const std::string coffee = add_product("Kaffee", "Kaffee", "coffee", "#D4A373", 50, 1);
    const std::string espresso = add_product("Espresso", "Kaffee", "coffee", "#B87333", 50, 2);
    const std::string cappuccino = add_product("Cappuccino", "Kaffee", "coffee", "#E0B15C", 70, 3);
    const std::string tea = add_product("Tee", "Tee", "tea", "#8FBC8F", 50, 4);
    const std::string water = add_product("Wasser", "Sonstiges", "water", "#7FA6D9", 30, 5);
    const std::string cocoa = add_product("Kakao", "Sonstiges", "cocoa", "#C07A48", 70, 6);

    const std::string products[] = {coffee, espresso, cappuccino, tea, water, cocoa};
    const std::string users[] = {anna, ben, clara, david, emma};
    const int counts[] = {12, 9, 7, 5, 3};

    int n = 0;
    for (size_t u = 0; u < 5; ++u) {
        for (int i = 0; i < counts[u]; ++i) {
            const std::string& product = products[(i + static_cast<int>(u)) % 6];
            kaffee::Timestamp ts = now_ - 86400 * 3 + n * 600;
            ledger_.purchase(users[u], product, "seed-buy-" + std::to_string(n), ts,
                             kaffee::TimeSource::Synced);
            ++n;
        }
    }

    for (int i = 0; i < 6; ++i) {
        ledger_.guest_sale(products[i % 6], "seed-guest-" + std::to_string(i),
                           now_ - 86400 * 2 + i * 900, kaffee::TimeSource::Synced);
    }
}

void FakeBackend::set_member_count(int total) {
    // Five members are seeded above; screenshots at 15/30/60 users add the rest.
    constexpr int kSeeded = 5;
    static const char* kNames[] = {
        "Felix", "Greta", "Hannes", "Ida",   "Jonas", "Katrin", "Lars", "Mia",  "Nico", "Olga",
        "Paul",  "Rosa",  "Sven",   "Tilda", "Uwe",   "Vera",   "Wim",  "Yara", "Zoe",  "Anton",
        "Berta", "Cem",   "Dora",   "Emil",  "Frida", "Gustav", "Hedi", "Ines", "Jule", "Klaus"};
    static const char* kColors[] = {"#D4A373", "#8FBC8F", "#7FA6D9", "#D98F8F",
                                    "#C9A0DC", "#E0B15C", "#B87333", "#C07A48"};
    constexpr int kNameCount = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));
    constexpr int kColorCount = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));
    std::vector<std::string> product_ids;
    for (const kaffee::Product& product : ledger_.products()) {
        if (product.active) product_ids.push_back(product.id);
    }
    if (product_ids.empty()) return;
    const int kProducts = static_cast<int>(product_ids.size());

    int have = 0;
    for (const kaffee::User& user : ledger_.users()) {
        if (user.active) ++have;
    }
    for (int n = have; n < total; ++n) {
        const int extra = n - kSeeded;
        const std::string name =
            extra < kNameCount ? std::string(kNames[extra]) : "Mitglied " + std::to_string(n + 1);
        const std::string color = kColors[extra % kColorCount];
        const int avatar_id = 1 + (extra * 7) % 30;
        auto created = ledger_.create_user(name, color, "seed-user-" + name, now_ - 86400 * 20,
                                           kaffee::TimeSource::Synced, avatar_id);
        if (!created.value) continue;
        const std::string id = created.value->user_id;
        ledger_.topup(id, 800, "Startguthaben", "seed-topup-" + name, now_ - 86400 * 10,
                      kaffee::TimeSource::Synced);

        // Deterministic spread of this month's bookings. A few busy members are
        // deliberately placed so `--order usage` visibly reorders the grid
        // instead of mirroring the creation order.
        int bookings = (extra * 7 + 3) % 12;
        if (extra == 2) {
            bookings = 21;
        } else if (extra == 7) {
            bookings = 17;
        } else if (extra == 13) {
            bookings = 14;
        }
        for (int i = 0; i < bookings; ++i) {
            const std::string& product = product_ids[(i + extra) % kProducts];
            ledger_.purchase(id, product,
                             "seed-extra-" + std::to_string(extra) + "-" + std::to_string(i),
                             now_ - 86400 * 2 + extra * 60 + i * 7, kaffee::TimeSource::Synced);
        }
    }
}

void FakeBackend::set_usage_order(bool enabled) {
    usage_order_ = enabled;
}

std::string FakeBackend::next_request(const char* prefix) {
    ++request_counter_;
    return std::string("sim-") + prefix + "-" + std::to_string(request_counter_);
}

void FakeBackend::notify() {
    if (change_callback_) change_callback_();
}

FakeBackend::UsersReturn FakeBackend::users() const {
    // Join order. With --order usage, app_settings() reports UserSortOrder::Usage
    // and the UI orders the tiles through the production code path.
    return ledger_.users();
}

FakeBackend::ProductsReturn FakeBackend::products() const {
    return ledger_.products();
}

std::optional<kaffee::User> FakeBackend::find_user(const std::string& id) const {
    return ledger_.find_user(id);
}

std::optional<kaffee::Product> FakeBackend::find_product(const std::string& id) const {
    return ledger_.find_product(id);
}

kaffee::Cents FakeBackend::balance(const std::string& user_id) const {
    return ledger_.balance(user_id);
}

kaffee::Result<kaffee::Event> FakeBackend::purchase(const std::string& user_id,
                                                    const std::string& product_id,
                                                    const std::string& request_id) {
    auto result =
        ledger_.purchase(user_id, product_id, request_id, now_, kaffee::TimeSource::Synced);
    if (result) notify();
    return result;
}

kaffee::Result<kaffee::Event> FakeBackend::guest_sale(const std::string& product_id,
                                                      const std::string& request_id) {
    auto result = ledger_.guest_sale(product_id, request_id, now_, kaffee::TimeSource::Synced);
    if (result) notify();
    return result;
}

kaffee::Leaderboard FakeBackend::leaderboard(kaffee::Timestamp from, kaffee::Timestamp to) const {
    return ledger_.leaderboard(from, to);
}

kaffee::SystemStats FakeBackend::stats() const {
    kaffee::SystemStats stats;
    stats.temperature_valid = true;
    stats.chip_temperature_c = 46.2f;
    stats.uptime_s = 3 * 86400 + 4 * 3600 + 21 * 60;
    stats.free_heap = 148 * 1024;
    stats.min_free_heap = 121 * 1024;
    stats.free_psram = 7'235'174;
    stats.storage.used_bytes = 1'258'291;
    stats.storage.total_bytes = 8 * 1024 * 1024;
    stats.storage.event_count = static_cast<uint32_t>(ledger_.events().size());
    stats.storage.mounted = true;
    stats.firmware_version = "0.4.0-sim";
    stats.build_date = "2026-11-11";
    stats.mac = "24:0A:C4:12:34:56";
    stats.network = network_status();
    stats.time = time_info();
    stats.last_backup_at = now_ - 86400 * 2;
    stats.drinks_today = 7;
    return stats;
}

kaffee::TimeInfo FakeBackend::time_info() const {
    kaffee::TimeInfo info;
    info.now = now_;
    info.source = kaffee::TimeSource::Synced;
    info.last_sync = now_ - 120;
    info.last_source = "browser";
    info.trusted = time_trusted_;
    return info;
}

kaffee::NetworkStatus FakeBackend::network_status() const {
    kaffee::NetworkStatus status;
    status.ap_active = ap_active_;
    status.ap_starting = ap_starting_;
    status.ap_remaining_s = ap_remaining_s_;
    status.ap_ssid = ap_ssid_;
    status.ap_password = ap_password_;
    status.ap_ip = ap_ip_;
    status.sta_connected = sta_connected_;
    status.sta_ip = sta_ip_;
    status.sta_rssi = sta_rssi_;
    return status;
}

kaffee::DisplaySettings FakeBackend::display_settings() const {
    return display_;
}

kaffee::Result<kaffee::Unit> FakeBackend::apply_display_settings(
    const kaffee::DisplaySettings& settings) {
    if (settings.rotation != 0 && settings.rotation != 90 && settings.rotation != 180 &&
        settings.rotation != 270) {
        return kaffee::Result<kaffee::Unit>::fail("rotation", "invalid rotation");
    }
    if (settings.brightness_active < 5 || settings.brightness_active > 100) {
        return kaffee::Result<kaffee::Unit>::fail("brightness", "invalid brightness");
    }
    display_ = settings;
    notify();
    return kaffee::Result<kaffee::Unit>::ok(kaffee::Unit{});
}

kaffee::Result<kaffee::Unit> FakeBackend::start_ap() {
    ap_active_ = true;
    ap_starting_ = false;
    ap_remaining_s_ = 900;
    notify();
    return kaffee::Result<kaffee::Unit>::ok(kaffee::Unit{});
}

kaffee::Result<kaffee::Unit> FakeBackend::stop_ap() {
    ap_active_ = false;
    ap_starting_ = false;
    ap_remaining_s_ = 0;
    notify();
    return kaffee::Result<kaffee::Unit>::ok(kaffee::Unit{});
}

kaffee::Result<kaffee::Unit> FakeBackend::extend_ap(int seconds) {
    if (!ap_active_) {
        return kaffee::Result<kaffee::Unit>::fail("ap", "AP is not active");
    }
    ap_remaining_s_ += seconds;
    if (ap_remaining_s_ > 3600) ap_remaining_s_ = 3600;
    notify();
    return kaffee::Result<kaffee::Unit>::ok(kaffee::Unit{});
}

kaffee::AppSettings FakeBackend::app_settings() const {
    kaffee::AppSettings settings;
    settings.guest_enabled = guest_enabled_;
    settings.currency = "EUR";
    settings.locale = "de-DE";
    settings.user_sort =
        usage_order_ ? kaffee::UserSortOrder::Usage : kaffee::UserSortOrder::Creation;
    return settings;
}

bool FakeBackend::must_change_admin_password() const {
    return false;
}

std::string FakeBackend::setup_password() const {
    return {};
}

kaffee::Result<kaffee::Event> FakeBackend::undo_booking(const std::string& event_id,
                                                        const std::string& request_id) {
    auto result =
        ledger_.void_event(event_id, "undo", request_id, now_, kaffee::TimeSource::Synced, "ui");
    if (result) notify();
    return result;
}

void FakeBackend::set_change_callback(kaffee::ChangeCallback callback) {
    change_callback_ = std::move(callback);
}

void FakeBackend::refresh() {
    notify();
}

void FakeBackend::set_rotation(int degrees) {
    display_.rotation = degrees;
}

void FakeBackend::set_brightness(int percent) {
    if (percent < 5) percent = 5;
    if (percent > 100) percent = 100;
    display_.brightness_active = percent;
}

void FakeBackend::set_time_trusted(bool trusted) {
    time_trusted_ = trusted;
}

void FakeBackend::set_guest_enabled(bool enabled) {
    guest_enabled_ = enabled;
}

int FakeBackend::count_events(kaffee::EventType type) const {
    int count = 0;
    for (const kaffee::Event& event : ledger_.events()) {
        if (event.type == type) ++count;
    }
    return count;
}

}  // namespace sim
