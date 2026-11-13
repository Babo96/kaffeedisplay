#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "api_internal.hpp"
#include "esp_log.h"
#include "kaffee/reports.hpp"

namespace kaffee {
namespace api {
namespace {

using nlohmann::json;

constexpr const char* kTag = "kaffee.api";
constexpr size_t kMaxNameLength = 40;
constexpr size_t kMaxNoteLength = 300;
constexpr Timestamp kMinTrustedTime = 1577836800;  // 2020-01-01T00:00:00Z
constexpr Timestamp kMaxTrustedTime = 4102444800;  // 2100-01-01T00:00:00Z

// --- Validated JSON field access ----------------------------------------

Result<std::string> get_string(const json& body, const char* key, bool required, size_t max_len) {
    if (!body.contains(key) || body[key].is_null()) {
        if (required) {
            return Result<std::string>::fail("invalid_input",
                                             std::string("Feld '") + key + "' fehlt");
        }
        return Result<std::string>::ok(std::string());
    }
    if (!body[key].is_string()) {
        return Result<std::string>::fail("invalid_input",
                                         std::string("Feld '") + key + "' muss Text sein");
    }
    std::string value = body[key].get<std::string>();
    if (value.size() > max_len) {
        return Result<std::string>::fail("invalid_input",
                                         std::string("Feld '") + key + "' ist zu lang");
    }
    return Result<std::string>::ok(std::move(value));
}

Result<Cents> get_cents(const json& body, const char* key, bool required, Cents min, Cents max) {
    if (!body.contains(key) || body[key].is_null()) {
        if (required) {
            return Result<Cents>::fail("invalid_input", std::string("Feld '") + key + "' fehlt");
        }
        return Result<Cents>::ok(static_cast<Cents>(0));
    }
    if (!body[key].is_number_integer() && !body[key].is_number_unsigned()) {
        return Result<Cents>::fail("invalid_input",
                                   std::string("Feld '") + key + "' muss eine Ganzzahl sein");
    }
    Cents value = body[key].get<Cents>();
    if (value < min || value > max) {
        return Result<Cents>::fail("invalid_input",
                                   std::string("Feld '") + key + "' liegt außerhalb des Bereichs");
    }
    return Result<Cents>::ok(value);
}

Result<bool> get_bool(const json& body, const char* key, bool fallback) {
    if (!body.contains(key) || body[key].is_null()) return Result<bool>::ok(fallback);
    if (!body[key].is_boolean()) {
        return Result<bool>::fail("invalid_input",
                                  std::string("Feld '") + key + "' muss bool sein");
    }
    return Result<bool>::ok(body[key].get<bool>());
}

bool validate_display(const DisplaySettings& display, Error& error) {
    if (display.rotation != 0 && display.rotation != 90 && display.rotation != 180 &&
        display.rotation != 270) {
        error = Error{"invalid_input", "Rotation muss 0, 90, 180 oder 270 sein"};
        return false;
    }
    if (display.brightness_active < 5 || display.brightness_active > 100) {
        error = Error{"invalid_input", "Aktive Helligkeit muss zwischen 5 und 100 liegen"};
        return false;
    }
    if (display.brightness_idle < 1 || display.brightness_idle > 100) {
        error = Error{"invalid_input", "Ruhehelligkeit muss zwischen 1 und 100 liegen"};
        return false;
    }
    if (display.dim_timeout_s < 0 || display.dim_timeout_s > 86400 ||
        display.screen_off_timeout_s < 0 || display.screen_off_timeout_s > 86400) {
        error = Error{"invalid_input", "Zeitüberschreitungen müssen zwischen 0 und 86400 liegen"};
        return false;
    }
    return true;
}

bool validate_settings(const Settings& settings, const Settings& base, Error& error) {
    const NetworkSettings& network = settings.network;
    if (!network.ap_ssid.empty() && network.ap_ssid.size() > 32) {
        error = Error{"invalid_input", "AP-Name darf höchstens 32 Zeichen haben"};
        return false;
    }
    // Only validate secrets that the request actually changed; an existing
    // stored secret must not block unrelated edits.
    if (network.ap_password != base.network.ap_password && !network.ap_password.empty() &&
        (network.ap_password.size() < 8 || network.ap_password.size() > 63)) {
        error = Error{"invalid_input", "AP-Passwort muss 8 bis 63 Zeichen haben"};
        return false;
    }
    if (network.sta_enabled && network.sta_ssid.empty()) {
        error = Error{"invalid_input", "STA-Netzwerkname fehlt"};
        return false;
    }
    if (network.sta_ssid.size() > 32) {
        error = Error{"invalid_input", "STA-Netzwerkname darf höchstens 32 Zeichen haben"};
        return false;
    }
    if (network.sta_password != base.network.sta_password && !network.sta_password.empty() &&
        (network.sta_password.size() < 8 || network.sta_password.size() > 63)) {
        error = Error{"invalid_input", "STA-Passwort muss 8 bis 63 Zeichen haben"};
        return false;
    }
    if (network.ap_window_s < 60 || network.ap_window_s > 3600) {
        error = Error{"invalid_input", "AP-Fenster muss zwischen 60 und 3600 Sekunden liegen"};
        return false;
    }
    if (network.hostname.empty() || network.hostname.size() > 32) {
        error = Error{"invalid_input", "Hostname ist ungültig"};
        return false;
    }
    if (settings.time.timezone.empty() || settings.time.timezone.size() > 64) {
        error = Error{"invalid_input", "Zeitzone ist ungültig"};
        return false;
    }
    if (settings.time.sntp_server.size() > 128) {
        error = Error{"invalid_input", "SNTP-Server ist zu lang"};
        return false;
    }
    if (settings.app.currency.size() != 3) {
        error = Error{"invalid_input", "Währung muss ein dreistelliger Code sein"};
        return false;
    }
    if (settings.app.locale.empty() || settings.app.locale.size() > 16) {
        error = Error{"invalid_input", "Sprache ist ungültig"};
        return false;
    }
    if (settings.app.max_open_debt_cents < 0) {
        error = Error{"invalid_input", "Maximale Schulden dürfen nicht negativ sein"};
        return false;
    }
    return true;
}

// --- Response builders ---------------------------------------------------

json ap_status_json(const NetworkStatus& network) {
    const std::string ip =
        network.ap_ip.empty() ? (network.ap_active ? "192.168.4.1" : "") : network.ap_ip;
    json status{{"active", network.ap_active},
                {"starting", network.ap_starting},
                {"remaining_seconds", network.ap_remaining_s},
                {"ssid", network.ap_active ? network.ap_ssid : std::string()},
                {"password", network.ap_active ? network.ap_password : std::string()},
                {"ip", ip},
                {"sta_connected", network.sta_connected},
                {"sta_ip", network.sta_ip},
                {"sta_rssi", network.sta_rssi}};
    if (network.ap_active && !ip.empty()) {
        const std::string url = "http://" + ip + "/";
        status["url"] = url;
        status["wifi_qr"] = wifi_qr_payload(network.ap_ssid, network.ap_password);
        status["url_qr"] = url_qr_payload(url);
    } else {
        status["url"] = "";
        status["wifi_qr"] = "";
        status["url_qr"] = "";
    }
    return status;
}

// Serialises events with the derived fields the contract exposes: `voided`
// and the balance after each user event. One pass over the ledger keeps this
// O(events) per request instead of O(events * page).
json events_json(KaffeeService& service, const std::vector<const Event*>& list) {
    std::set<std::string> voided;
    for (const Event& event : service.events()) {
        if (event.type == EventType::Void && !event.reverses.empty()) voided.insert(event.reverses);
    }

    std::set<std::string> targets;
    for (const Event* event : list) targets.insert(event->id);

    std::map<std::string, Cents> balance_after;
    if (!targets.empty()) {
        std::map<std::string, Cents> balances;
        for (const Event& event : service.events()) {
            if (event.user_id.empty()) continue;
            balances[event.user_id] += event.amount_cents;
            if (targets.count(event.id) != 0) balance_after[event.id] = balances[event.user_id];
        }
    }

    json array = json::array();
    for (const Event* event : list) {
        json item = event_to_json(*event);
        item["voided"] = voided.count(event->id) != 0;
        auto found = balance_after.find(event->id);
        if (!event->user_id.empty() && found != balance_after.end()) {
            item["balance_after_cents"] = found->second;
        }
        array.push_back(std::move(item));
    }
    return array;
}

json event_json(KaffeeService& service, const Event& event) {
    json array = events_json(service, {&event});
    return array.at(0);
}

const Expense* find_expense(KaffeeService& service, const std::string& id) {
    for (const Expense& expense : service.expenses()) {
        if (expense.id == id) return &expense;
    }
    return nullptr;
}

json expense_or_event(KaffeeService& service, const Event& event) {
    if (const Expense* expense = find_expense(service, event.expense_id)) {
        return expense_to_json(*expense);
    }
    json fallback = event_to_json(event);
    fallback["id"] = event.expense_id;
    return fallback;
}

json cash_count_from_event(const Event& event) {
    return json{{"id", event.id},
                {"ts", event.ts},
                {"counted_cents", event.counted_cents},
                {"expected_cents", event.expected_cents},
                {"difference_cents", event.counted_cents - event.expected_cents},
                {"note", event.note}};
}

// --- Auth ----------------------------------------------------------------

esp_err_t handle_health(RequestContext& ctx) {
    SystemStats stats = ctx.server.platform().stats();
    return send_json(ctx.req, 200, json{{"status", "ok"}, {"version", stats.firmware_version}});
}

json session_json(KaffeeService& service, const Session& session) {
    return json{
        {"username", service.settings().admin.username.empty() ? std::string("admin")
                                                               : service.settings().admin.username},
        {"csrf_token", session.csrf_token},
        {"expires_at", session.expires_at},
        {"must_change_password", service.must_change_admin_password()}};
}

esp_err_t handle_login(RequestContext& ctx) {
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));

    Result<std::string> username = get_string(*body.value, "username", true, 64);
    if (!username) return send_service_error(ctx.req, result_error(username));
    Result<std::string> password = get_string(*body.value, "password", true, 256);
    if (!password) return send_service_error(ctx.req, result_error(password));

    const std::string ip = client_ip(ctx.req);
    Result<Session> session = ctx.server.service().login(*username.value, *password.value, ip);
    if (!session) {
        ESP_LOGW(kTag, "login failed from %s: %s", ip.c_str(), result_error(session).code.c_str());
        return send_service_error(ctx.req, result_error(session));
    }

    const Timestamp now = ctx.server.platform().time_info().now;
    set_session_cookie(ctx.req, session.value->token, session.value->expires_at, now);
    return send_json(ctx.req, 200, session_json(ctx.server.service(), *session.value));
}

esp_err_t handle_logout(RequestContext& ctx) {
    Result<Unit> result = ctx.server.service().logout(ctx.token);
    if (!result) return send_service_error(ctx.req, result_error(result));
    clear_session_cookie(ctx.req);
    return send_no_content(ctx.req);
}

esp_err_t handle_me(RequestContext& ctx) {
    // validate_session() already slid the idle window; re-issue the cookie so
    // the browser Max-Age follows the refreshed absolute expiry.
    const Timestamp now = ctx.server.platform().time_info().now;
    const std::string& token = ctx.session.token.empty() ? ctx.token : ctx.session.token;
    set_session_cookie(ctx.req, token, ctx.session.expires_at, now);
    return send_json(ctx.req, 200, session_json(ctx.server.service(), ctx.session));
}

// --- Users ---------------------------------------------------------------

esp_err_t handle_users_list(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    std::map<std::string, Timestamp> last_booking;
    std::map<std::string, Cents> balances;
    for (const Event& event : service.events()) {
        if (event.user_id.empty()) continue;
        balances[event.user_id] += event.amount_cents;
        if (event.type == EventType::Purchase) {
            auto& value = last_booking[event.user_id];
            if (event.ts > value) value = event.ts;
        }
    }

    json array = json::array();
    for (const User& user : service.users()) {
        json item = user_to_json(user);
        auto balance = balances.find(user.id);
        item["balance_cents"] = balance == balances.end() ? 0 : balance->second;
        auto found = last_booking.find(user.id);
        if (found != last_booking.end()) item["last_booking_at"] = found->second;
        array.push_back(std::move(item));
    }
    return send_json(ctx.req, 200, array);
}

esp_err_t handle_users_create(RequestContext& ctx) {
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> name = get_string(*body.value, "name", true, kMaxNameLength);
    if (!name) return send_service_error(ctx.req, result_error(name));
    std::string trimmed_name = trim(*name.value);
    if (trimmed_name.empty()) {
        return send_error(ctx.req, 400, "invalid_input", "Name darf nicht leer sein");
    }
    Result<std::string> color = get_string(*body.value, "color", false, 7);
    if (!color) return send_service_error(ctx.req, result_error(color));
    std::string user_color = color.value->empty() ? "#D4A373" : *color.value;
    if (!is_valid_color(user_color)) {
        return send_error(ctx.req, 400, "invalid_input", "Farbe muss das Format #RRGGBB haben");
    }

    Result<Cents> avatar = get_cents(*body.value, "avatar_id", false, 0, kMaxUserAvatarId);
    if (!avatar) return send_service_error(ctx.req, result_error(avatar));

    // An omitted or null avatar_id requests a random preset; an explicit zero
    // uses initials. Non-null values were range-checked above.
    const bool has_avatar =
        body.value->contains("avatar_id") && !(*body.value)["avatar_id"].is_null();
    const int avatar_id = has_avatar ? static_cast<int>(*avatar.value) : -1;
    auto& service = ctx.server.service();
    auto event = service.create_user(trimmed_name, user_color, ctx.request_id, avatar_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto user = ctx.server.service().find_user(event.value->user_id);
    if (!user) return send_error(ctx.req, 500, "internal_error", "Benutzer nicht gefunden");
    return send_json(ctx.req, 201, user_to_json(*user));
}

esp_err_t handle_users_update(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    auto existing = service.find_user(id);
    if (!existing) return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");

    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> name = get_string(*body.value, "name", false, kMaxNameLength);
    if (!name) return send_service_error(ctx.req, result_error(name));
    Result<std::string> color = get_string(*body.value, "color", false, 7);
    if (!color) return send_service_error(ctx.req, result_error(color));
    Result<Cents> avatar = get_cents(*body.value, "avatar_id", false, 0, kMaxUserAvatarId);
    if (!avatar) return send_service_error(ctx.req, result_error(avatar));
    Result<bool> active = get_bool(*body.value, "active", existing->active);
    if (!active) return send_service_error(ctx.req, result_error(active));

    std::string new_name = name.value->empty() ? existing->name : trim(*name.value);
    if (new_name.empty()) {
        return send_error(ctx.req, 400, "invalid_input", "Name darf nicht leer sein");
    }
    std::string new_color = color.value->empty() ? existing->color : *color.value;
    if (!is_valid_color(new_color)) {
        return send_error(ctx.req, 400, "invalid_input", "Farbe muss das Format #RRGGBB haben");
    }

    // -1 means the PATCH left the avatar out (absent or null): preserve the
    // current choice. An explicit zero resets to initials.
    const bool has_avatar =
        body.value->contains("avatar_id") && !(*body.value)["avatar_id"].is_null();
    const int avatar_id = has_avatar ? static_cast<int>(*avatar.value) : -1;
    const bool is_active = *active.value;
    auto event = service.update_user(id, new_name, new_color, is_active, ctx.request_id, avatar_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto updated = service.find_user(id);
    if (!updated) return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    return send_json(ctx.req, 200, user_to_json(*updated));
}

esp_err_t handle_users_archive(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_user(id)) {
        return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    }
    Result<Event> event = service.archive_user(id, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto archived = service.find_user(id);
    if (!archived) return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    return send_json(ctx.req, 200, user_to_json(*archived));
}

esp_err_t handle_users_topup(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_user(id)) {
        return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    }
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<Cents> amount = get_cents(*body.value, "amount_cents", true, 1, 1000000);
    if (!amount) return send_service_error(ctx.req, result_error(amount));
    Result<std::string> note = get_string(*body.value, "note", false, 200);
    if (!note) return send_service_error(ctx.req, result_error(note));

    Result<Event> event = service.topup(id, *amount.value, *note.value, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

esp_err_t handle_users_adjust(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_user(id)) {
        return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    }
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<Cents> amount = get_cents(*body.value, "amount_cents", true, -1000000, 1000000);
    if (!amount) return send_service_error(ctx.req, result_error(amount));
    Result<std::string> note = get_string(*body.value, "note", true, 200);
    if (!note) return send_service_error(ctx.req, result_error(note));
    if (trim(*note.value).size() < 3) {
        return send_error(ctx.req, 400, "invalid_input", "Notiz muss mindestens 3 Zeichen haben");
    }

    Result<Event> event = service.adjust(id, *amount.value, *note.value, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

esp_err_t handle_users_transactions(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_user(id)) {
        return send_error(ctx.req, 404, "not_found", "Benutzer nicht gefunden");
    }
    const std::string query = req_query(ctx.req);
    int64_t limit = query_int(query, "limit").value_or(50);
    if (limit < 1) limit = 1;
    if (limit > 200) limit = 200;
    auto before = query_int(query, "before");
    if (before && *before < 0) before.reset();

    std::vector<const Event*> selected;
    const auto& events = service.events();
    for (auto it = events.rbegin();
         it != events.rend() && selected.size() < static_cast<size_t>(limit); ++it) {
        if (it->user_id != id) continue;
        if (before && it->seq >= static_cast<uint64_t>(*before)) continue;
        selected.push_back(&*it);
    }
    return send_json(ctx.req, 200, events_json(service, selected));
}

// --- Products ------------------------------------------------------------

esp_err_t handle_products_list(RequestContext& ctx) {
    const bool include_archived = query_flag(req_query(ctx.req), "include_archived", false);
    json array = json::array();
    for (const Product& product : ctx.server.service().products()) {
        if (!include_archived && !product.active) continue;
        array.push_back(product_to_json(product));
    }
    return send_json(ctx.req, 200, array);
}

struct ProductInput {
    std::string name;
    std::string category;
    std::string icon;
    std::string color;
    Cents price_cents = 0;
    int sort = 0;
    bool active = true;
};

Result<ProductInput> parse_product_input(const json& body, const Product* base) {
    Result<std::string> name = get_string(body, "name", base == nullptr, kMaxNameLength);
    if (!name)
        return Result<ProductInput>::fail(result_error(name).code, result_error(name).message);
    Result<std::string> category = get_string(body, "category", base == nullptr, kMaxNameLength);
    if (!category) {
        return Result<ProductInput>::fail(result_error(category).code,
                                          result_error(category).message);
    }
    Result<std::string> icon = get_string(body, "icon", false, kMaxNameLength);
    if (!icon)
        return Result<ProductInput>::fail(result_error(icon).code, result_error(icon).message);
    Result<std::string> color = get_string(body, "color", false, 7);
    if (!color)
        return Result<ProductInput>::fail(result_error(color).code, result_error(color).message);
    Result<Cents> price = get_cents(body, "price_cents", base == nullptr, 0, 100000);
    if (!price)
        return Result<ProductInput>::fail(result_error(price).code, result_error(price).message);
    Result<Cents> sort = get_cents(body, "sort", false, -100000, 100000);
    if (!sort)
        return Result<ProductInput>::fail(result_error(sort).code, result_error(sort).message);
    Result<bool> active = get_bool(body, "active", base == nullptr ? true : base->active);
    if (!active)
        return Result<ProductInput>::fail(result_error(active).code, result_error(active).message);

    ProductInput input;
    input.name = base ? base->name : "";
    input.category = base ? base->category : "";
    input.icon = base ? base->icon : "";
    input.color = base ? base->color : "#8B5E3C";
    input.sort = base ? base->sort : 0;
    input.price_cents = base ? base->price_cents : 0;
    input.active = *active.value;

    if (!name.value->empty()) input.name = trim(*name.value);
    if (!category.value->empty()) input.category = *category.value;
    if (!icon.value->empty()) input.icon = *icon.value;
    if (!color.value->empty()) input.color = *color.value;
    if (body.contains("price_cents")) input.price_cents = *price.value;
    if (body.contains("sort")) input.sort = static_cast<int>(*sort.value);

    if (input.name.empty()) {
        return Result<ProductInput>::fail("invalid_input", "Name darf nicht leer sein");
    }
    if (input.category.empty()) {
        return Result<ProductInput>::fail("invalid_input", "Kategorie darf nicht leer sein");
    }
    if (!is_valid_color(input.color)) {
        return Result<ProductInput>::fail("invalid_input", "Farbe muss das Format #RRGGBB haben");
    }
    return Result<ProductInput>::ok(std::move(input));
}

esp_err_t handle_products_create(RequestContext& ctx) {
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<ProductInput> input = parse_product_input(*body.value, nullptr);
    if (!input) return send_service_error(ctx.req, result_error(input));

    Result<Event> event = ctx.server.service().create_product(
        input.value->name, input.value->category, input.value->icon, input.value->color,
        input.value->price_cents, input.value->sort, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto product = ctx.server.service().find_product(event.value->product_id);
    if (!product) return send_error(ctx.req, 500, "internal_error", "Produkt nicht gefunden");
    return send_json(ctx.req, 201, product_to_json(*product));
}

esp_err_t handle_products_update(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    auto existing = service.find_product(id);
    if (!existing) return send_error(ctx.req, 404, "not_found", "Produkt nicht gefunden");

    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<ProductInput> input = parse_product_input(*body.value, &*existing);
    if (!input) return send_service_error(ctx.req, result_error(input));

    Result<Event> event = service.update_product(
        id, input.value->name, input.value->category, input.value->icon, input.value->color,
        input.value->price_cents, input.value->sort, input.value->active, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    auto product = service.find_product(id);
    if (!product) return send_error(ctx.req, 404, "not_found", "Produkt nicht gefunden");
    return send_json(ctx.req, 200, product_to_json(*product));
}

esp_err_t handle_products_delete(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (!service.find_product(id)) {
        return send_error(ctx.req, 404, "not_found", "Produkt nicht gefunden");
    }
    Result<Event> event = service.delete_product(id, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_no_content(ctx.req);
}

// --- Transactions --------------------------------------------------------

esp_err_t handle_transactions_list(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string query = req_query(ctx.req);
    const std::string user_id = query_param(query, "user_id").value_or("");
    const std::string type_name = query_param(query, "type").value_or("");
    auto from = query_int(query, "from");
    auto to = query_int(query, "to");
    int64_t limit = query_int(query, "limit").value_or(100);
    int64_t offset = query_int(query, "offset").value_or(0);
    if (limit < 1) limit = 1;
    if (limit > 500) limit = 500;
    if (offset < 0) offset = 0;

    EventType type = EventType::Unknown;
    const bool filter_type = !type_name.empty();
    if (filter_type) {
        type = Ledger::event_type_from_name(type_name);
        if (type == EventType::Unknown && lower_ascii(type_name) != "unknown") {
            return send_error(ctx.req, 400, "invalid_input", "Unbekannter Buchungstyp");
        }
    }

    std::vector<const Event*> selected;
    const auto& events = service.events();
    size_t skipped = 0;
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        if (!user_id.empty() && it->user_id != user_id) continue;
        if (filter_type && it->type != type) continue;
        if (from && it->ts < *from) continue;
        if (to && it->ts >= *to) continue;
        if (skipped < static_cast<size_t>(offset)) {
            ++skipped;
            continue;
        }
        if (selected.size() >= static_cast<size_t>(limit)) break;
        selected.push_back(&*it);
    }
    return send_json(ctx.req, 200, events_json(service, selected));
}

esp_err_t handle_transactions_void(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> reason = get_string(*body.value, "reason", true, 200);
    if (!reason) return send_service_error(ctx.req, result_error(reason));
    if (trim(*reason.value).size() < 3) {
        return send_error(ctx.req, 400, "invalid_input", "Grund muss mindestens 3 Zeichen haben");
    }
    Result<Event> event = service.void_event(id, trim(*reason.value), ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

// --- Expenses ------------------------------------------------------------

struct ExpenseInput {
    std::string category;
    std::string description;
    std::string vendor;
    std::string note;
    Cents amount_cents = 0;
};

Result<ExpenseInput> parse_expense_input(const json& body, const Expense* base) {
    Result<std::string> category = get_string(body, "category", base == nullptr, kMaxNameLength);
    if (!category) {
        return Result<ExpenseInput>::fail(result_error(category).code,
                                          result_error(category).message);
    }
    Result<std::string> description = get_string(body, "description", base == nullptr, 120);
    if (!description) {
        return Result<ExpenseInput>::fail(result_error(description).code,
                                          result_error(description).message);
    }
    Result<std::string> vendor = get_string(body, "vendor", false, 80);
    if (!vendor) {
        return Result<ExpenseInput>::fail(result_error(vendor).code, result_error(vendor).message);
    }
    Result<std::string> note = get_string(body, "note", false, kMaxNoteLength);
    if (!note)
        return Result<ExpenseInput>::fail(result_error(note).code, result_error(note).message);
    Result<Cents> amount = get_cents(body, "amount_cents", base == nullptr, 1, 10000000);
    if (!amount)
        return Result<ExpenseInput>::fail(result_error(amount).code, result_error(amount).message);

    ExpenseInput input;
    if (base != nullptr) {
        input.category = base->category;
        input.description = base->description;
        input.vendor = base->vendor;
        input.note = base->note;
        input.amount_cents = base->amount_cents;
    }
    if (!category.value->empty()) input.category = *category.value;
    if (!description.value->empty()) input.description = *description.value;
    if (!vendor.value->empty()) input.vendor = *vendor.value;
    if (body.contains("note")) input.note = *note.value;
    if (body.contains("amount_cents")) input.amount_cents = *amount.value;

    if (input.category.empty()) {
        return Result<ExpenseInput>::fail("invalid_input", "Kategorie darf nicht leer sein");
    }
    if (input.description.empty()) {
        return Result<ExpenseInput>::fail("invalid_input", "Beschreibung darf nicht leer sein");
    }
    return Result<ExpenseInput>::ok(std::move(input));
}

esp_err_t handle_expenses_list(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string query = req_query(ctx.req);
    auto from = query_int(query, "from");
    auto to = query_int(query, "to");
    const bool include_voided = query_flag(query, "include_voided", false);

    json array = json::array();
    const auto& expenses = service.expenses();
    for (auto it = expenses.rbegin(); it != expenses.rend(); ++it) {
        if (!include_voided && it->voided) continue;
        if (from && it->ts < *from) continue;
        if (to && it->ts >= *to) continue;
        array.push_back(expense_to_json(*it));
    }
    return send_json(ctx.req, 200, array);
}

esp_err_t handle_expenses_create(RequestContext& ctx) {
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<ExpenseInput> input = parse_expense_input(*body.value, nullptr);
    if (!input) return send_service_error(ctx.req, result_error(input));

    Result<Event> event = ctx.server.service().add_expense(
        input.value->category, input.value->description, input.value->vendor,
        input.value->amount_cents, input.value->note, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 201, expense_or_event(ctx.server.service(), *event.value));
}

esp_err_t handle_expenses_update(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    const Expense* existing = find_expense(service, id);
    if (existing == nullptr) return send_error(ctx.req, 404, "not_found", "Ausgabe nicht gefunden");

    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<ExpenseInput> input = parse_expense_input(*body.value, existing);
    if (!input) return send_service_error(ctx.req, result_error(input));

    Result<Event> event = service.update_expense(
        id, input.value->category, input.value->description, input.value->vendor,
        input.value->amount_cents, input.value->note, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    if (const Expense* updated = find_expense(service, id)) {
        return send_json(ctx.req, 200, expense_to_json(*updated));
    }
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

esp_err_t handle_expenses_void(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string id = ctx.params.get("id").value_or("");
    if (find_expense(service, id) == nullptr) {
        return send_error(ctx.req, 404, "not_found", "Ausgabe nicht gefunden");
    }
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> reason = get_string(*body.value, "reason", true, 200);
    if (!reason) return send_service_error(ctx.req, result_error(reason));
    if (trim(*reason.value).size() < 3) {
        return send_error(ctx.req, 400, "invalid_input", "Grund muss mindestens 3 Zeichen haben");
    }
    Result<Event> event = service.void_expense(id, trim(*reason.value), ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 200, event_json(service, *event.value));
}

// --- Cash counts and stats ----------------------------------------------

esp_err_t handle_cash_counts_list(RequestContext& ctx) {
    json array = json::array();
    const auto& counts = ctx.server.service().cash_counts();
    for (auto it = counts.rbegin(); it != counts.rend(); ++it) {
        array.push_back(cash_count_to_json(*it));
    }
    return send_json(ctx.req, 200, array);
}

esp_err_t handle_cash_counts_create(RequestContext& ctx) {
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<Cents> counted = get_cents(*body.value, "counted_cents", true, 0, 100000000);
    if (!counted) return send_service_error(ctx.req, result_error(counted));
    Result<std::string> note = get_string(*body.value, "note", false, 200);
    if (!note) return send_service_error(ctx.req, result_error(note));

    Result<Event> event =
        ctx.server.service().record_cash_count(*counted.value, *note.value, ctx.request_id);
    if (!event) return send_service_error(ctx.req, result_error(event));
    return send_json(ctx.req, 201, cash_count_from_event(*event.value));
}

esp_err_t handle_summary(RequestContext& ctx) {
    return send_json(ctx.req, 200, summary_to_json(ctx.server.service().summary()));
}

esp_err_t handle_leaderboard(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    const std::string query = req_query(ctx.req);
    TimeRange range;
    if (auto month = query_param(query, "month")) {
        auto parsed = parse_month_range(*month);
        if (!parsed) return send_error(ctx.req, 400, "invalid_input", "Monat muss YYYY-MM sein");
        range = *parsed;
    } else {
        const Timestamp now = ctx.server.platform().time_info().now;
        if (now <= 0) {
            range.from = 0;
            range.to = std::numeric_limits<Timestamp>::max();
        } else {
            int year = 0;
            unsigned month = 0;
            unsigned day = 0;
            civil_from_days((now + berlin_utc_offset(now)) / 86400, year, month, day);
            auto parsed = month_range_utc(year, static_cast<int>(month));
            range = parsed.value_or(TimeRange{now - 30 * 86400, now + 1});
        }
    }
    return send_json(ctx.req, 200, leaderboard_to_json(service.leaderboard(range.from, range.to)));
}

esp_err_t handle_reports(RequestContext& ctx) {
    const std::string kind_name = ctx.params.get("kind").value_or("");
    auto kind = reports::report_kind_from_name(kind_name);
    if (!kind) return send_error(ctx.req, 404, "not_found", "Unbekannter Bericht");

    const std::string query = req_query(ctx.req);
    const std::string period_name = query_param(query, "period").value_or("month");
    auto period_kind = reports::period_kind_from_name(period_name);
    if (!period_kind) {
        return send_error(ctx.req, 400, "invalid_input", "Unbekannter Zeitraum");
    }

    reports::Period period;
    if (*period_kind == reports::PeriodKind::Custom) {
        auto from = query_int(query, "from");
        auto to = query_int(query, "to");
        if (!from || !to) {
            return send_error(ctx.req, 400, "invalid_input",
                              "from und to sind für einen eigenen Zeitraum erforderlich");
        }
        period = reports::custom(*from, *to);
    } else {
        const Timestamp now = ctx.server.platform().time_info().now;
        if (*period_kind != reports::PeriodKind::All && now <= 0) {
            return send_error(ctx.req, 409, "time_unset",
                              "Gerätezeit ist nicht gesetzt; bitte zuerst synchronisieren");
        }
        period = reports::period(*period_kind, now);
    }
    if (!reports::valid(period)) {
        return send_error(ctx.req, 400, "invalid_input", "Zeitraum ist ungültig");
    }

    const std::shared_ptr<const Ledger> ledger_snapshot = ctx.server.service().ledger_snapshot();
    if (!ledger_snapshot) {
        return send_error(ctx.req, 500, "internal_error", "Bericht konnte nicht erzeugt werden");
    }
    const Ledger& ledger = *ledger_snapshot;

    std::string format = query_param(query, "format").value_or("");
    if (format.empty()) {
        const std::string accept = req_header(ctx.req, "Accept").value_or("");
        format = accept.find("text/csv") != std::string::npos ? "csv_excel" : "json";
    }

    if (format == "json") {
        nlohmann::json payload = reports::to_json(ledger, *kind, period);
        const TimeInfo time = ctx.server.platform().time_info();
        nlohmann::json quality{{"source", Ledger::time_source_name(time.source)}};
        if (time.last_sync > 0) quality["last_sync"] = time.last_sync;
        if (time.now > 0) quality["trusted"] = time.trusted;
        payload["time_quality"] = std::move(quality);
        return send_json(ctx.req, 200, payload);
    }

    reports::CsvFormat csv_format;
    if (format == "csv_excel") {
        csv_format = reports::CsvFormat::Excel;
    } else if (format == "csv_raw") {
        csv_format = reports::CsvFormat::Raw;
    } else {
        return send_error(ctx.req, 400, "invalid_input", "Unbekanntes Berichtsformat");
    }

    // Reports are small at kiosk scale; buffering one CSV is simpler than
    // holding the service lock across chunked socket writes.
    std::string body;
    reports::Sink sink{[&body](std::string_view chunk) {
        body.append(chunk.data(), chunk.size());
        return true;
    }};
    auto written = reports::write_csv(ledger, *kind, period, csv_format, sink);
    if (!written) {
        return send_error(ctx.req, 500, "internal_error", "Bericht konnte nicht erzeugt werden");
    }
    const std::string disposition = content_disposition(reports::report_filename(*kind, period));
    return send_chunked_text(ctx.req, 200, reports::csv_content_type(csv_format), body,
                             disposition.c_str());
}

// --- Settings ------------------------------------------------------------

esp_err_t handle_settings_get(RequestContext& ctx) {
    return send_json(ctx.req, 200, settings_to_json(ctx.server.service().settings(), false));
}

esp_err_t handle_settings_put(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));

    // PUT applies the provided subtrees and keeps omitted values. This matches
    // the all-optional Settings schema; write-only secrets are never cleared by
    // an empty string.
    Settings base = service.settings();
    Settings next;
    try {
        next = settings_from_json(*body.value, base);
    } catch (const nlohmann::json::exception&) {
        return send_error(ctx.req, 400, "invalid_input", "Einstellungen sind ungültig");
    }

    // Empty write-only secrets keep the stored value (contract).
    if (body.value->contains("network")) {
        const json& network = (*body.value)["network"];
        if (network.contains("ap_password") && network["ap_password"].is_string() &&
            network["ap_password"].get<std::string>().empty()) {
            next.network.ap_password = base.network.ap_password;
        }
        if (network.contains("sta_password") && network["sta_password"].is_string() &&
            network["sta_password"].get<std::string>().empty()) {
            next.network.sta_password = base.network.sta_password;
        }
    }

    Error error;
    if (!validate_display(next.display, error)) return send_service_error(ctx.req, error);
    if (!validate_settings(next, base, error)) return send_service_error(ctx.req, error);

    Result<Unit> saved = service.save_settings(next, ctx.request_id);
    if (!saved) return send_service_error(ctx.req, result_error(saved));
    return send_json(ctx.req, 200, settings_to_json(service.settings(), false));
}

esp_err_t handle_display_get(RequestContext& ctx) {
    return send_json(ctx.req, 200,
                     display_settings_to_json(ctx.server.service().display_settings()));
}

esp_err_t handle_display_put(RequestContext& ctx) {
    KaffeeService& service = ctx.server.service();
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));

    DisplaySettings next = service.display_settings();
    if (body.value->contains("rotation")) {
        Result<Cents> rotation = get_cents(*body.value, "rotation", false, 0, 270);
        if (!rotation) return send_service_error(ctx.req, result_error(rotation));
        next.rotation = static_cast<int>(*rotation.value);
    }
    if (body.value->contains("brightness_active")) {
        Result<Cents> value = get_cents(*body.value, "brightness_active", false, 5, 100);
        if (!value) return send_service_error(ctx.req, result_error(value));
        next.brightness_active = static_cast<int>(*value.value);
    }
    if (body.value->contains("brightness_idle")) {
        Result<Cents> value = get_cents(*body.value, "brightness_idle", false, 1, 100);
        if (!value) return send_service_error(ctx.req, result_error(value));
        next.brightness_idle = static_cast<int>(*value.value);
    }
    if (body.value->contains("dim_timeout_s")) {
        Result<Cents> value = get_cents(*body.value, "dim_timeout_s", false, 0, 86400);
        if (!value) return send_service_error(ctx.req, result_error(value));
        next.dim_timeout_s = static_cast<int>(*value.value);
    }
    if (body.value->contains("screen_off_timeout_s")) {
        Result<Cents> value = get_cents(*body.value, "screen_off_timeout_s", false, 0, 86400);
        if (!value) return send_service_error(ctx.req, result_error(value));
        next.screen_off_timeout_s = static_cast<int>(*value.value);
    }

    Error error;
    if (!validate_display(next, error)) return send_service_error(ctx.req, error);

    Result<Unit> applied = service.apply_display_settings(next);
    if (!applied) return send_service_error(ctx.req, result_error(applied));
    return send_json(ctx.req, 200, display_settings_to_json(service.display_settings()));
}

esp_err_t handle_password_change(RequestContext& ctx) {
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<std::string> current = get_string(*body.value, "current_password", true, 256);
    if (!current) return send_service_error(ctx.req, result_error(current));
    Result<std::string> next = get_string(*body.value, "new_password", true, 256);
    if (!next) return send_service_error(ctx.req, result_error(next));
    if (next.value->size() < 10) {
        return send_error(ctx.req, 400, "invalid_input",
                          "Neues Passwort muss mindestens 10 Zeichen haben");
    }
    Result<Unit> changed =
        ctx.server.service().change_admin_password(*current.value, *next.value, ctx.request_id);
    if (!changed) return send_service_error(ctx.req, result_error(changed));
    // The service owns the session map; dropping our token here at least
    // revokes the session that performed the change. TODO(T-001): have
    // change_admin_password() clear all sessions when the service lands.
    (void)ctx.server.service().logout(ctx.token);
    clear_session_cookie(ctx.req);
    return send_no_content(ctx.req);
}

// --- Time ----------------------------------------------------------------

esp_err_t handle_time_get(RequestContext& ctx) {
    return send_json(ctx.req, 200, time_info_json(ctx.server.platform().time_info()));
}

esp_err_t handle_time_sync(RequestContext& ctx) {
    Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    Result<Cents> epoch_ms =
        get_cents(*body.value, "epoch_ms", true, std::numeric_limits<int64_t>::min(),
                  std::numeric_limits<int64_t>::max());
    if (!epoch_ms) return send_service_error(ctx.req, result_error(epoch_ms));

    const Timestamp seconds = *epoch_ms.value / 1000;
    if (seconds < kMinTrustedTime || seconds > kMaxTrustedTime) {
        return send_error(ctx.req, 400, "invalid_input",
                          "Zeitstempel liegt außerhalb des Bereichs");
    }
    Result<Unit> synced = ctx.server.service().sync_time(seconds, "browser");
    if (!synced) return send_service_error(ctx.req, result_error(synced));
    return send_json(ctx.req, 200, time_info_json(ctx.server.platform().time_info()));
}

// --- AP ------------------------------------------------------------------

esp_err_t handle_ap_status(RequestContext& ctx) {
    return send_json(ctx.req, 200, ap_status_json(ctx.server.service().network_status()));
}

esp_err_t handle_ap_start(RequestContext& ctx) {
    Result<Unit> started = ctx.server.service().start_ap();
    if (!started) return send_service_error(ctx.req, result_error(started));
    return send_json(ctx.req, 200, ap_status_json(ctx.server.service().network_status()));
}

esp_err_t handle_ap_stop(RequestContext& ctx) {
    Result<Unit> stopped = ctx.server.service().stop_ap();
    if (!stopped) return send_service_error(ctx.req, result_error(stopped));
    return send_no_content(ctx.req);
}

esp_err_t handle_ap_extend(RequestContext& ctx) {
    Cents seconds = 600;
    if (ctx.req->content_len > 0) {
        Result<json> body = read_json_object(ctx.req, kMaxJsonBody);
        if (!body) return send_service_error(ctx.req, result_error(body));
        if (body.value->contains("seconds")) {
            Result<Cents> parsed = get_cents(*body.value, "seconds", false, 60, 3600);
            if (!parsed) return send_service_error(ctx.req, result_error(parsed));
            seconds = *parsed.value;
        }
    }
    Result<Unit> extended = ctx.server.service().extend_ap(static_cast<int>(seconds));
    if (!extended) return send_service_error(ctx.req, result_error(extended));
    return send_json(ctx.req, 200, ap_status_json(ctx.server.service().network_status()));
}

// --- System --------------------------------------------------------------

esp_err_t handle_device(RequestContext& ctx) {
    // Service stats add storage, last_backup_at and drinks_today on top of the
    // platform diagnostics.
    return send_json(ctx.req, 200, device_status_json(ctx.server.service().stats()));
}

esp_err_t handle_display_test(RequestContext& ctx) {
    Result<Unit> shown = ctx.server.platform().show_display_test();
    if (!shown) return send_service_error(ctx.req, result_error(shown));
    return send_no_content(ctx.req);
}

esp_err_t handle_logs(RequestContext& ctx) {
    int64_t limit = query_int(req_query(ctx.req), "limit").value_or(200);
    if (limit < 1) limit = 1;
    if (limit > 500) limit = 500;
    json lines = json::array();
    for (const std::string& line : log_ring_lines(static_cast<size_t>(limit))) {
        lines.push_back(line);
    }
    return send_json(ctx.req, 200, json{{"lines", std::move(lines)}});
}

esp_err_t handle_backup(RequestContext& ctx) {
    Result<std::string> data = ctx.server.service().export_backup();
    if (!data) return send_service_error(ctx.req, result_error(data));
    const Timestamp now = ctx.server.platform().time_info().now;
    const std::string disposition = content_disposition(backup_filename(now));
    return send_chunked_text(ctx.req, 200, "application/json; charset=utf-8", *data.value,
                             disposition.c_str());
}

esp_err_t handle_restore(RequestContext& ctx) {
    Result<std::string> body = read_body(ctx.req, kMaxRestoreBody);
    if (!body) return send_service_error(ctx.req, result_error(body));
    if (body.value->empty()) {
        return send_error(ctx.req, 400, "invalid_input", "Sicherung fehlt");
    }
    Result<Unit> restored = ctx.server.service().import_backup(*body.value, ctx.request_id);
    if (!restored) return send_service_error(ctx.req, result_error(restored));
    return send_no_content(ctx.req);
}

esp_err_t handle_factory_reset(RequestContext& ctx) {
    // Authenticated + CSRF + request id checked by the dispatcher. Erase in a
    // helper task after the response so the connection is not cut mid-body.
    if (!ctx.server.schedule_factory_reset()) {
        return send_error(ctx.req, 500, "internal_error",
                          "Werkseinstellungen konnten nicht gestartet werden");
    }
    ESP_LOGW(kTag, "factory reset requested from %s", client_ip(ctx.req).c_str());
    return send_no_content(ctx.req);
}

}  // namespace

// Handler bindings. MUST stay in the same order as `route_specs()` in
// api_routes.cpp; dispatch validates the lengths at runtime.
const std::vector<RouteHandler>& route_handlers() {
    static const std::vector<RouteHandler> kHandlers = {
        &handle_health,
        &handle_login,
        &handle_logout,
        &handle_me,

        &handle_users_list,
        &handle_users_create,
        &handle_users_update,
        &handle_users_archive,
        &handle_users_topup,
        &handle_users_adjust,
        &handle_users_transactions,

        &handle_products_list,
        &handle_products_create,
        &handle_products_update,
        &handle_products_delete,

        &handle_transactions_list,
        &handle_transactions_void,

        &handle_expenses_list,
        &handle_expenses_create,
        &handle_expenses_update,
        &handle_expenses_void,

        &handle_cash_counts_list,
        &handle_cash_counts_create,

        &handle_summary,
        &handle_leaderboard,
        &handle_reports,

        &handle_settings_get,
        &handle_settings_put,
        &handle_display_get,
        &handle_display_put,
        &handle_password_change,

        &handle_time_get,
        &handle_time_sync,

        &handle_ap_status,
        &handle_ap_start,
        &handle_ap_stop,
        &handle_ap_extend,

        &handle_device,
        &handle_display_test,
        &handle_logs,
        &handle_backup,
        &handle_restore,
        &handle_factory_reset,
        &handle_ota,
    };
    return kHandlers;
}

}  // namespace api
}  // namespace kaffee
