#include "kaffee/json.hpp"

namespace kaffee {
namespace {

void put_if(nlohmann::json& j, const char* key, const std::string& value) {
    if (!value.empty()) j[key] = value;
}

}  // namespace

nlohmann::json user_to_json(const User& u) {
    nlohmann::json j{{"id", u.id},
                     {"name", u.name},
                     {"color", u.color},
                     {"active", u.active},
                     {"created_at", u.created_at}};
    j["avatar_id"] = u.avatar_id;
    if (u.archived_at) j["archived_at"] = u.archived_at;
    return j;
}

User user_from_json(const nlohmann::json& j) {
    User u;
    u.id = j.value("id", "");
    u.name = j.value("name", "");
    u.color = j.value("color", "");
    u.avatar_id = j.value("avatar_id", 0);
    u.active = j.value("active", true);
    u.created_at = j.value("created_at", static_cast<Timestamp>(0));
    u.archived_at = j.value("archived_at", static_cast<Timestamp>(0));
    return u;
}

nlohmann::json product_to_json(const Product& p) {
    return nlohmann::json{{"id", p.id},
                          {"name", p.name},
                          {"category", p.category},
                          {"icon", p.icon},
                          {"color", p.color},
                          {"price_cents", p.price_cents},
                          {"sort", p.sort},
                          {"active", p.active},
                          {"created_at", p.created_at},
                          {"updated_at", p.updated_at}};
}

Product product_from_json(const nlohmann::json& j) {
    Product p;
    p.id = j.value("id", "");
    p.name = j.value("name", "");
    p.category = j.value("category", "");
    p.icon = j.value("icon", "");
    p.color = j.value("color", "");
    p.price_cents = j.value("price_cents", static_cast<Cents>(0));
    p.sort = j.value("sort", 0);
    p.active = j.value("active", true);
    p.created_at = j.value("created_at", static_cast<Timestamp>(0));
    p.updated_at = j.value("updated_at", static_cast<Timestamp>(0));
    return p;
}

nlohmann::json event_to_json(const Event& e) {
    nlohmann::json j{{"seq", e.seq},
                     {"id", e.id},
                     {"ts", e.ts},
                     {"time_source", Ledger::time_source_name(e.time_source)},
                     {"type", Ledger::event_type_name(e.type)}};
    put_if(j, "user_id", e.user_id);
    put_if(j, "user_name", e.user_name);
    put_if(j, "user_color", e.user_color);
    // Omit 0 to preserve hashes of events recorded by older firmware.
    if (e.user_avatar_id != 0) j["user_avatar_id"] = e.user_avatar_id;
    if (!e.user_id.empty()) j["user_active"] = e.user_active;
    put_if(j, "product_id", e.product_id);
    put_if(j, "product_name", e.product_name);
    put_if(j, "product_category", e.product_category);
    put_if(j, "product_icon", e.product_icon);
    put_if(j, "product_color", e.product_color);
    if (!e.product_id.empty()) {
        j["product_sort"] = e.product_sort;
        j["product_active"] = e.product_active;
    }
    if (e.unit_amount_cents != 0) j["unit_amount_cents"] = e.unit_amount_cents;
    if (e.amount_cents != 0) j["amount_cents"] = e.amount_cents;
    put_if(j, "expense_id", e.expense_id);
    put_if(j, "expense_category", e.expense_category);
    put_if(j, "expense_description", e.expense_description);
    put_if(j, "expense_vendor", e.expense_vendor);
    if (e.counted_cents != 0) j["counted_cents"] = e.counted_cents;
    if (e.expected_cents != 0) j["expected_cents"] = e.expected_cents;
    put_if(j, "payment", e.payment);
    put_if(j, "note", e.note);
    put_if(j, "actor", e.actor);
    put_if(j, "request_id", e.request_id);
    put_if(j, "reverses", e.reverses);
    put_if(j, "meta", e.meta_json);
    put_if(j, "prev_hash", e.prev_hash);
    put_if(j, "hash", e.hash);
    return j;
}

Event event_from_json(const nlohmann::json& j) {
    Event e;
    e.seq = j.value("seq", static_cast<uint64_t>(0));
    e.id = j.value("id", "");
    e.ts = j.value("ts", static_cast<Timestamp>(0));
    e.time_source = Ledger::time_source_from_name(j.value("time_source", "unset"));
    e.type = Ledger::event_type_from_name(j.value("type", ""));
    e.user_id = j.value("user_id", "");
    e.user_name = j.value("user_name", "");
    e.user_color = j.value("user_color", "");
    e.user_avatar_id = j.value("user_avatar_id", 0);
    e.user_active = j.value("user_active", true);
    e.product_id = j.value("product_id", "");
    e.product_name = j.value("product_name", "");
    e.product_category = j.value("product_category", "");
    e.product_icon = j.value("product_icon", "");
    e.product_color = j.value("product_color", "");
    e.product_sort = j.value("product_sort", 0);
    e.product_active = j.value("product_active", true);
    e.unit_amount_cents = j.value("unit_amount_cents", static_cast<Cents>(0));
    e.amount_cents = j.value("amount_cents", static_cast<Cents>(0));
    e.expense_id = j.value("expense_id", "");
    e.expense_category = j.value("expense_category", "");
    e.expense_description = j.value("expense_description", "");
    e.expense_vendor = j.value("expense_vendor", "");
    e.counted_cents = j.value("counted_cents", static_cast<Cents>(0));
    e.expected_cents = j.value("expected_cents", static_cast<Cents>(0));
    e.payment = j.value("payment", "");
    e.note = j.value("note", "");
    e.actor = j.value("actor", "");
    e.request_id = j.value("request_id", "");
    e.reverses = j.value("reverses", "");
    e.meta_json = j.value("meta", "");
    e.prev_hash = j.value("prev_hash", "");
    e.hash = j.value("hash", "");
    return e;
}

nlohmann::json expense_to_json(const Expense& e) {
    return nlohmann::json{{"id", e.id},
                          {"ts", e.ts},
                          {"category", e.category},
                          {"description", e.description},
                          {"vendor", e.vendor},
                          {"amount_cents", e.amount_cents},
                          {"note", e.note},
                          {"voided", e.voided}};
}

Expense expense_from_json(const nlohmann::json& j) {
    Expense e;
    e.id = j.value("id", "");
    e.ts = j.value("ts", static_cast<Timestamp>(0));
    e.category = j.value("category", "");
    e.description = j.value("description", "");
    e.vendor = j.value("vendor", "");
    e.amount_cents = j.value("amount_cents", static_cast<Cents>(0));
    e.note = j.value("note", "");
    e.voided = j.value("voided", false);
    return e;
}

nlohmann::json cash_count_to_json(const CashCount& c) {
    return nlohmann::json{{"id", c.id},
                          {"ts", c.ts},
                          {"counted_cents", c.counted_cents},
                          {"expected_cents", c.expected_cents},
                          {"difference_cents", c.counted_cents - c.expected_cents},
                          {"note", c.note}};
}

nlohmann::json summary_to_json(const Summary& s) {
    return nlohmann::json{{"cash_expected_cents", s.cash_expected},
                          {"prepaid_cents", s.prepaid},
                          {"debts_cents", s.debts},
                          {"net_liabilities_cents", s.net_liabilities},
                          {"equity_cents", s.equity},
                          {"member_sales_cents", s.member_sales},
                          {"guest_sales_cents", s.guest_sales},
                          {"topups_cents", s.topups},
                          {"expenses_cents", s.expenses},
                          {"member_items", s.member_items},
                          {"guest_items", s.guest_items}};
}

nlohmann::json leaderboard_to_json(const Leaderboard& l) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& e : l.entries) {
        entries.push_back(nlohmann::json{
            {"user_id", e.user_id}, {"name", e.name}, {"color", e.color}, {"count", e.count}});
    }
    return nlohmann::json{
        {"from", l.from}, {"to", l.to}, {"guest_count", l.guest_count}, {"entries", entries}};
}

nlohmann::json display_settings_to_json(const DisplaySettings& s) {
    return nlohmann::json{{"rotation", s.rotation},
                          {"brightness_active", s.brightness_active},
                          {"brightness_idle", s.brightness_idle},
                          {"dim_timeout_s", s.dim_timeout_s},
                          {"screen_off_timeout_s", s.screen_off_timeout_s}};
}

DisplaySettings display_settings_from_json(const nlohmann::json& j) {
    DisplaySettings s;
    s.rotation = j.value("rotation", 0);
    s.brightness_active = j.value("brightness_active", 100);
    s.brightness_idle = j.value("brightness_idle", 20);
    s.dim_timeout_s = j.value("dim_timeout_s", 60);
    s.screen_off_timeout_s = j.value("screen_off_timeout_s", 600);
    return s;
}

nlohmann::json settings_to_json(const Settings& s, bool include_secrets) {
    nlohmann::json network{
        {"ap_ssid", s.network.ap_ssid},   {"ap_window_s", s.network.ap_window_s},
        {"hostname", s.network.hostname}, {"sta_enabled", s.network.sta_enabled},
        {"sta_ssid", s.network.sta_ssid}, {"napt_enabled", s.network.napt_enabled}};
    if (include_secrets) {
        network["ap_password"] = s.network.ap_password;
        network["sta_password"] = s.network.sta_password;
    }
    nlohmann::json j{{"display", display_settings_to_json(s.display)},
                     {"network", network},
                     {"time",
                      {{"timezone", s.time.timezone},
                       {"sntp_enabled", s.time.sntp_enabled},
                       {"sntp_server", s.time.sntp_server}}},
                     {"app",
                      {{"currency", s.app.currency},
                       {"locale", s.app.locale},
                       {"guest_enabled", s.app.guest_enabled},
                       {"max_open_debt_cents", s.app.max_open_debt_cents},
                       {"low_balance_warning_cents", s.app.low_balance_warning_cents},
                       {"user_sort", user_sort_to_string(s.app.user_sort)}}}};
    if (include_secrets) {
        j["admin"] = {{"username", s.admin.username},
                      {"password_hash", s.admin.password_hash},
                      {"salt", s.admin.salt},
                      {"iterations", s.admin.iterations}};
    }
    return j;
}

const char* user_sort_to_string(UserSortOrder order) {
    return order == UserSortOrder::Usage ? "usage" : "creation";
}

UserSortOrder user_sort_from_string(std::string_view value, UserSortOrder fallback) {
    if (value == "usage") return UserSortOrder::Usage;
    if (value == "creation") return UserSortOrder::Creation;
    return fallback;
}

Settings settings_from_json(const nlohmann::json& j, const Settings& base) {
    Settings s = base;
    if (j.contains("display")) s.display = display_settings_from_json(j["display"]);
    if (j.contains("network")) {
        const auto& n = j["network"];
        s.network.ap_ssid = n.value("ap_ssid", s.network.ap_ssid);
        s.network.ap_window_s = n.value("ap_window_s", s.network.ap_window_s);
        s.network.hostname = n.value("hostname", s.network.hostname);
        s.network.sta_enabled = n.value("sta_enabled", s.network.sta_enabled);
        s.network.sta_ssid = n.value("sta_ssid", s.network.sta_ssid);
        s.network.napt_enabled = n.value("napt_enabled", s.network.napt_enabled);
        if (n.contains("ap_password")) s.network.ap_password = n.value("ap_password", "");
        if (n.contains("sta_password")) s.network.sta_password = n.value("sta_password", "");
    }
    if (j.contains("time")) {
        const auto& t = j["time"];
        s.time.timezone = t.value("timezone", s.time.timezone);
        s.time.sntp_enabled = t.value("sntp_enabled", s.time.sntp_enabled);
        s.time.sntp_server = t.value("sntp_server", s.time.sntp_server);
    }
    if (j.contains("app")) {
        const auto& a = j["app"];
        s.app.currency = a.value("currency", s.app.currency);
        s.app.locale = a.value("locale", s.app.locale);
        s.app.guest_enabled = a.value("guest_enabled", s.app.guest_enabled);
        s.app.max_open_debt_cents = a.value("max_open_debt_cents", s.app.max_open_debt_cents);
        s.app.low_balance_warning_cents =
            a.value("low_balance_warning_cents", s.app.low_balance_warning_cents);
        if (a.contains("user_sort") && a["user_sort"].is_string()) {
            // Unknown spellings (older/newer firmware, hand-edited backups) keep
            // the base value instead of failing the whole settings apply.
            s.app.user_sort =
                user_sort_from_string(a["user_sort"].get<std::string>(), base.app.user_sort);
        }
    }
    if (j.contains("admin") && j["admin"].is_object()) {
        const auto& a = j["admin"];
        s.admin.username = a.value("username", s.admin.username);
        s.admin.password_hash = a.value("password_hash", s.admin.password_hash);
        s.admin.salt = a.value("salt", s.admin.salt);
        s.admin.iterations = a.value("iterations", s.admin.iterations);
    }
    return s;
}

}  // namespace kaffee
