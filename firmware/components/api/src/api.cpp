#include "api_internal.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace kaffee {
namespace api {
namespace {

constexpr const char* kTag = "kaffee.api";

// Response helper for the request id echo. esp_http_server handles one request
// at a time per server, so a plain string is sufficient and avoids a
// thread_local object with a global constructor on the device.
std::string t_response_request_id;

httpd_method_t to_httpd_method(RouteMethod method) {
    switch (method) {
        case RouteMethod::Get:
            return HTTP_GET;
        case RouteMethod::Post:
            return HTTP_POST;
        case RouteMethod::Put:
            return HTTP_PUT;
        case RouteMethod::Patch:
            return HTTP_PATCH;
        case RouteMethod::Delete:
            return HTTP_DELETE;
    }
    return HTTP_GET;
}

// esp_http_server names its worker task "httpd"; httpd_stop() must not be called
// from that task, so /ap/stop defers server teardown to a helper task.
bool is_httpd_task() {
    const char* name = pcTaskGetName(nullptr);
    return name != nullptr && std::strcmp(name, "httpd") == 0;
}

void deferred_stop_task(void* arg) {
    auto* self = static_cast<ApiServerImpl*>(arg);
    // Give the current response a moment to flush before the server stops.
    vTaskDelay(pdMS_TO_TICKS(50));
    (void)self->stop();
    vTaskDelete(nullptr);
}

esp_err_t send_captive_probe(ApiServerImpl& server, httpd_req_t* req, CaptiveProbe probe) {
    if (probe == CaptiveProbe::Generate204) {
        httpd_resp_set_status(req, status_text(204));
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        set_security_headers(req);
        return httpd_resp_send(req, nullptr, 0);
    }
    if (probe == CaptiveProbe::Redirect) {
        // Captive probes only make sense while the AP is active. The net
        // component owns the AP lifecycle; this route only reflects it.
        NetworkStatus network = server.service().network_status();
        if (!network.ap_active) {
            return send_error(req, 404, "not_found", "Ressource nicht gefunden");
        }
        const std::string ip = network.ap_ip.empty() ? "192.168.4.1" : network.ap_ip;
        const std::string location = "http://" + ip + "/";
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", location.c_str());
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        set_security_headers(req);
        return httpd_resp_send(req, nullptr, 0);
    }
    return send_error(req, 404, "not_found", "Ressource nicht gefunden");
}

esp_err_t dispatch_entry(httpd_req_t* req) {
    auto* self = static_cast<ApiServerImpl*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;

    const std::string path = req_path(req);
    if (path.rfind("/api/", 0) != 0) {
        const CaptiveProbe probe = captive_probe_kind(path);
        if (probe != CaptiveProbe::None) return send_captive_probe(*self, req, probe);
        if (req->method != HTTP_GET) {
            return send_error(req, 405, "method_not_allowed", "Methode nicht erlaubt");
        }
        return handle_static(req);
    }

    const std::vector<RouteSpec>& specs = route_specs();
    const std::vector<RouteHandler>& handlers = route_handlers();
    if (specs.size() != handlers.size()) {
        ESP_LOGE(kTag, "route table mismatch: %u specs, %u handlers",
                 static_cast<unsigned>(specs.size()), static_cast<unsigned>(handlers.size()));
        return send_error(req, 500, "internal_error", "Interner Fehler");
    }

    bool path_matched = false;
    for (size_t i = 0; i < specs.size(); ++i) {
        const RouteSpec& spec = specs[i];
        RouteParams params;
        if (!match_path(spec.pattern, path, params)) continue;
        path_matched = true;
        if (to_httpd_method(spec.method) != req->method) continue;

        RequestContext ctx{*self, req, std::move(params), {}, {}, false, {}};

        if (spec.auth != AuthPolicy::None) {
            auto cookie = req_header(req, "Cookie");
            auto token = cookie ? cookie_value(*cookie, "kaffee_session") : std::nullopt;
            if (!token || token->empty()) {
                return send_error(req, 401, "unauthorized", "Anmeldung erforderlich");
            }
            Result<Session> session = self->service().validate_session(*token);
            if (!session) {
                return send_error(req, 401, "unauthorized", "Sitzung abgelaufen");
            }
            ctx.token = *token;
            ctx.session = *session.value;
            ctx.authenticated = true;

            if (self->service().must_change_admin_password() &&
                !route_allowed_when_password_must_change(spec)) {
                return send_error(req, 403, "password_change_required",
                                  "Passwort muss zuerst geändert werden");
            }

            if (spec.auth == AuthPolicy::SessionCsrf) {
                auto csrf = req_header(req, "X-CSRF-Token");
                if (!csrf || csrf->empty() || !self->service().validate_csrf(*token, *csrf)) {
                    return send_error(req, 403, "csrf_failed",
                                      "CSRF-Token fehlt oder ist ungültig");
                }
            }
        }

        auto request_id = req_header(req, "X-Request-Id");
        std::string trimmed = request_id ? trim(*request_id) : std::string();
        if (!trimmed.empty() && is_valid_request_id(trimmed)) ctx.request_id = trimmed;
        if (spec.require_request_id) {
            if (trimmed.empty()) {
                return send_error(req, 400, "missing_request_id",
                                  "Header X-Request-Id ist erforderlich");
            }
            if (!is_valid_request_id(trimmed)) {
                return send_error(req, 400, "invalid_request_id",
                                  "Header X-Request-Id ist ungültig");
            }
        }

        t_response_request_id = ctx.request_id;
        esp_err_t result = ESP_OK;
        try {
            result = handlers[i](ctx);
        } catch (...) {
            result = send_error(req, 500, "internal_error", "Interner Fehler");
        }
        t_response_request_id.clear();
        return result;
    }

    if (path_matched) {
        return send_error(req, 405, "method_not_allowed", "Methode nicht erlaubt");
    }
    return send_error(req, 404, "not_found", "Ressource nicht gefunden");
}

void reboot_task(void* arg) {
    const uint32_t delay_ms = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    esp_restart();
}

void factory_reset_task(void* arg) {
    auto* self = static_cast<ApiServerImpl*>(arg);
    // Let the 204 response flush before the AP/HTTP server goes away.
    vTaskDelay(pdMS_TO_TICKS(300));

    auto storage = self->service().factory_reset();
    if (!storage) {
        ESP_LOGE(kTag, "factory reset: storage/settings reset failed: %s",
                 storage.error ? storage.error->message.c_str() : "unknown");
    }
    auto network = self->platform().reset_network_credentials();
    if (!network) {
        ESP_LOGE(kTag, "factory reset: network credential reset failed: %s",
                 network.error ? network.error->message.c_str() : "unknown");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

}  // namespace

// --- Lifecycle -----------------------------------------------------------

ApiServerImpl::ApiServerImpl(KaffeeService& service, IPlatform& platform)
    : service_(service), platform_(platform) {}

ApiServerImpl::~ApiServerImpl() {
    stop();
    // A deferred stop may still be queued from a request handler.
    for (int i = 0; i < 100 && stop_pending_; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

Result<Unit> ApiServerImpl::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (server_ != nullptr) return Result<Unit>::ok(Unit{});

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 8;
    // Ledger writes (product create/update, reports) use nlohmann::json on this
    // task; the default 4 KiB and the earlier 10 KiB both overflowed. Keep the
    // headroom explicit here because esp_http_server has no Kconfig stack knob.
    config.stack_size = 16384;
    config.lru_purge_enable = true;
    // Uploads (OTA) must not pin a socket for long once the client stalls.
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("unavailable", "HTTP-Server konnte nicht gestartet werden");
    }

    httpd_uri_t catch_all{};
    catch_all.uri = "/*";
    catch_all.method = static_cast<httpd_method_t>(HTTP_ANY);
    catch_all.handler = &dispatch_entry;
    catch_all.user_ctx = this;
    err = httpd_register_uri_handler(server, &catch_all);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_register_uri_handler failed: %s", esp_err_to_name(err));
        httpd_stop(server);
        return Result<Unit>::fail("unavailable", "HTTP-Routen konnten nicht registriert werden");
    }

    server_ = server;
    log_ring_install();
    ESP_LOGI(kTag, "HTTP API listening on port %d", config.server_port);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> ApiServerImpl::stop() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (server_ == nullptr) return Result<Unit>::ok(Unit{});
    if (is_httpd_task()) {
        // /ap/stop runs inside the httpd task; calling httpd_stop() here would
        // deadlock. Defer teardown and let the current response finish.
        if (!stop_pending_.exchange(true)) {
            if (xTaskCreate(&deferred_stop_task, "api_stop", 3072, this, 5, nullptr) != pdPASS) {
                stop_pending_ = false;
                return Result<Unit>::fail("unavailable",
                                          "HTTP-Server konnte nicht gestoppt werden");
            }
        }
        return Result<Unit>::ok(Unit{});
    }
    return stop_locked();
}

Result<Unit> ApiServerImpl::stop_locked() {
    if (server_ == nullptr) {
        stop_pending_ = false;
        return Result<Unit>::ok(Unit{});
    }
    httpd_handle_t server = server_;
    server_ = nullptr;
    esp_err_t err = httpd_stop(server);
    log_ring_uninstall();
    stop_pending_ = false;
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "httpd_stop failed: %s", esp_err_to_name(err));
        return Result<Unit>::fail("unavailable", "HTTP-Server konnte nicht gestoppt werden");
    }
    ESP_LOGI(kTag, "HTTP API stopped");
    return Result<Unit>::ok(Unit{});
}

bool ApiServerImpl::running() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return server_ != nullptr;
}

void ApiServerImpl::schedule_reboot(uint32_t delay_ms) {
    xTaskCreate(&reboot_task, "api_reboot", 2048, reinterpret_cast<void*>(delay_ms), 5, nullptr);
}

bool ApiServerImpl::schedule_factory_reset() {
    return xTaskCreate(&factory_reset_task, "api_factory_reset", 4096, this, 5, nullptr) == pdPASS;
}

// --- Responses -----------------------------------------------------------

const char* status_text(int status) {
    switch (status) {
        case 200:
            return "200 OK";
        case 201:
            return "201 Created";
        case 204:
            return "204 No Content";
        case 304:
            return "304 Not Modified";
        case 400:
            return "400 Bad Request";
        case 401:
            return "401 Unauthorized";
        case 403:
            return "403 Forbidden";
        case 404:
            return "404 Not Found";
        case 405:
            return "405 Method Not Allowed";
        case 409:
            return "409 Conflict";
        case 411:
            return "411 Length Required";
        case 413:
            return "413 Payload Too Large";
        case 415:
            return "415 Unsupported Media Type";
        case 429:
            return "429 Too Many Requests";
        case 500:
            return "500 Internal Server Error";
        case 501:
            return "501 Not Implemented";
        case 503:
            return "503 Service Unavailable";
        default:
            return "500 Internal Server Error";
    }
}

void set_security_headers(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    if (!t_response_request_id.empty()) {
        httpd_resp_set_hdr(req, "X-Request-Id", t_response_request_id.c_str());
    }
}

esp_err_t send_json_text(httpd_req_t* req, int status, std::string_view body) {
    httpd_resp_set_status(req, status_text(status));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    set_security_headers(req);
    return httpd_resp_send(req, body.data(), body.size());
}

esp_err_t send_json(httpd_req_t* req, int status, const nlohmann::json& body) {
    const std::string text = body.dump();
    return send_json_text(req, status, text);
}

esp_err_t send_error(httpd_req_t* req, int status, std::string_view code,
                     std::string_view message) {
    return send_json_text(req, status, json_error_body(code, message));
}

esp_err_t send_service_error(httpd_req_t* req, const Error& error) {
    const ErrorMapping mapping = map_service_error(error);
    return send_error(req, mapping.status, mapping.code, mapping.message);
}

esp_err_t send_no_content(httpd_req_t* req) {
    httpd_resp_set_status(req, status_text(204));
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    set_security_headers(req);
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t send_chunked_text(httpd_req_t* req, int status, const char* content_type,
                            std::string_view body, const char* content_disposition) {
    httpd_resp_set_status(req, status_text(status));
    httpd_resp_set_type(req, content_type);
    if (content_disposition != nullptr) {
        httpd_resp_set_hdr(req, "Content-Disposition", content_disposition);
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    set_security_headers(req);

    constexpr size_t kSlice = 4096;
    size_t offset = 0;
    while (offset < body.size()) {
        size_t len = std::min(kSlice, body.size() - offset);
        esp_err_t err = httpd_resp_send_chunk(req, body.data() + offset, len);
        if (err != ESP_OK) return err;
        offset += len;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// --- Requests ------------------------------------------------------------

std::optional<std::string> req_header(httpd_req_t* req, const char* name) {
    size_t len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0) return std::nullopt;
    std::string value(len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK) {
        return std::nullopt;
    }
    value.resize(len);
    return value;
}

std::string req_path(httpd_req_t* req) {
    std::string uri(req->uri);
    size_t query = uri.find('?');
    if (query != std::string::npos) uri.resize(query);
    return uri;
}

std::string req_query(httpd_req_t* req) {
    std::string uri(req->uri);
    size_t query = uri.find('?');
    if (query == std::string::npos) return {};
    return uri.substr(query + 1);
}

std::string client_ip(httpd_req_t* req) {
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) return "0.0.0.0";
    struct sockaddr_in addr = {};
    socklen_t len = sizeof(addr);
    if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
        return "0.0.0.0";
    }
    const uint32_t host = ntohl(addr.sin_addr.s_addr);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", static_cast<unsigned>((host >> 24) & 0xff),
                  static_cast<unsigned>((host >> 16) & 0xff),
                  static_cast<unsigned>((host >> 8) & 0xff), static_cast<unsigned>(host & 0xff));
    return buf;
}

Result<std::string> read_body(httpd_req_t* req, size_t limit) {
    if (req->content_len == 0) return Result<std::string>::ok(std::string());
    if (req->content_len > limit) {
        return Result<std::string>::fail("payload_too_large", "Anfrage ist zu groß");
    }
    std::string body(req->content_len, '\0');
    size_t received = 0;
    int timeouts = 0;
    while (received < body.size()) {
        int got = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > 3) {
                return Result<std::string>::fail("bad_request", "Zeitüberschreitung beim Lesen");
            }
            continue;
        }
        if (got <= 0) {
            return Result<std::string>::fail("bad_request", "Anfrage konnte nicht gelesen werden");
        }
        received += static_cast<size_t>(got);
    }
    return Result<std::string>::ok(std::move(body));
}

Result<nlohmann::json> read_json_body(httpd_req_t* req, size_t limit) {
    Result<std::string> body = read_body(req, limit);
    if (!body)
        return Result<nlohmann::json>::fail(result_error(body).code, result_error(body).message);
    if (body.value->empty()) {
        return Result<nlohmann::json>::fail("invalid_input", "JSON-Körper fehlt");
    }
    nlohmann::json parsed = nlohmann::json::parse(*body.value, nullptr, false);
    if (parsed.is_discarded()) {
        return Result<nlohmann::json>::fail("invalid_input", "JSON ist ungültig");
    }
    return Result<nlohmann::json>::ok(std::move(parsed));
}

Result<nlohmann::json> read_json_object(httpd_req_t* req, size_t limit) {
    Result<nlohmann::json> body = read_json_body(req, limit);
    if (!body) return body;
    if (!body.value->is_object()) {
        return Result<nlohmann::json>::fail("invalid_input", "JSON-Objekt erwartet");
    }
    return body;
}

void set_session_cookie(httpd_req_t* req, std::string_view token, Timestamp expires_at,
                        Timestamp now) {
    constexpr int64_t kFallbackMaxAge = 30 * 60;
    int64_t max_age = kFallbackMaxAge;
    if (expires_at > 0 && now > 0 && expires_at > now) max_age = expires_at - now;
    if (max_age < 60) max_age = 60;
    if (max_age > 8 * 3600) max_age = 8 * 3600;

    if (token.empty()) {
        ESP_LOGE(kTag, "refusing to set an empty session cookie");
        return;
    }

    // httpd_resp_set_hdr() stores the value pointer, it does not copy it. The
    // buffer must stay valid until httpd sends the response, so it cannot be a
    // local. esp_http_server handles one request at a time per server.
    static char cookie[320];
    const int length = std::snprintf(
        cookie, sizeof(cookie),
        "kaffee_session=%.*s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%lld",
        static_cast<int>(token.size()), token.data(), static_cast<long long>(max_age));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(cookie)) {
        ESP_LOGE(kTag, "session cookie buffer too small (token_len=%u)",
                 static_cast<unsigned>(token.size()));
        return;
    }
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    ESP_LOGI(kTag, "session cookie set: token_len=%u header_len=%d",
             static_cast<unsigned>(token.size()), length);
}

void clear_session_cookie(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Set-Cookie",
                       "kaffee_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}

// --- Log ring ------------------------------------------------------------

namespace {

constexpr size_t kLogLines = 48;
constexpr size_t kLogLineLength = 160;

struct LogRing {
    char lines[kLogLines][kLogLineLength];
    size_t next;
    size_t count;
    bool installed;
    vprintf_like_t previous;
};

LogRing g_log_ring;
portMUX_TYPE g_log_lock = portMUX_INITIALIZER_UNLOCKED;

int log_hook(const char* format, va_list args) {
    char line[kLogLineLength];
    va_list copy;
    va_copy(copy, args);
    int written = std::vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);
    if (written > 0) {
        size_t length = std::strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (contains_secret_marker(line)) {
            static constexpr char kRedacted[] = "[Zeile wegen Geheimnis gefiltert]";
            std::strncpy(line, kRedacted, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            length = std::strlen(line);
        }
        portENTER_CRITICAL(&g_log_lock);
        std::memcpy(g_log_ring.lines[g_log_ring.next], line, length + 1);
        g_log_ring.next = (g_log_ring.next + 1) % kLogLines;
        if (g_log_ring.count < kLogLines) ++g_log_ring.count;
        portEXIT_CRITICAL(&g_log_lock);
    }
    vprintf_like_t previous = nullptr;
    portENTER_CRITICAL(&g_log_lock);
    previous = g_log_ring.previous;
    portEXIT_CRITICAL(&g_log_lock);
    if (previous != nullptr) return previous(format, args);
    return written;
}

}  // namespace

void log_ring_install() {
    portENTER_CRITICAL(&g_log_lock);
    if (!g_log_ring.installed) {
        g_log_ring.previous = esp_log_set_vprintf(&log_hook);
        g_log_ring.installed = true;
    }
    portEXIT_CRITICAL(&g_log_lock);
}

void log_ring_uninstall() {
    portENTER_CRITICAL(&g_log_lock);
    if (g_log_ring.installed) {
        esp_log_set_vprintf(g_log_ring.previous);
        g_log_ring.installed = false;
        g_log_ring.previous = nullptr;
    }
    portEXIT_CRITICAL(&g_log_lock);
}

std::vector<std::string> log_ring_lines(size_t limit) {
    std::vector<std::string> out;
    if (limit == 0) return out;
    if (limit > kLogLines) limit = kLogLines;

    // Allocate the snapshot before taking the spinlock: no dynamic allocation
    // (and no long memcpy) while interrupts are disabled.
    std::vector<char> snapshot(limit * kLogLineLength);
    size_t available = 0;
    portENTER_CRITICAL(&g_log_lock);
    available = g_log_ring.count < limit ? g_log_ring.count : limit;
    const size_t first = (g_log_ring.next + kLogLines - available) % kLogLines;
    for (size_t i = 0; i < available; ++i) {
        std::memcpy(snapshot.data() + i * kLogLineLength, g_log_ring.lines[(first + i) % kLogLines],
                    kLogLineLength);
    }
    portEXIT_CRITICAL(&g_log_lock);

    out.reserve(available);
    for (size_t i = 0; i < available; ++i) {
        out.emplace_back(snapshot.data() + i * kLogLineLength);
    }
    return out;
}

}  // namespace api
}  // namespace kaffee

namespace kaffee {

Result<std::unique_ptr<ApiServer>> ApiServer::create(KaffeeService& service, IPlatform& platform) {
    return Result<std::unique_ptr<ApiServer>>::ok(
        std::make_unique<api::ApiServerImpl>(service, platform));
}

}  // namespace kaffee
