#include "kaffee/api_routes.hpp"

#include <cstring>

namespace kaffee {
namespace api {
namespace {

bool same(std::string_view value, const char* expected) {
    return value == expected;
}

}  // namespace

const std::vector<RouteSpec>& route_specs() {
    static const std::vector<RouteSpec> kSpecs = {
        {RouteMethod::Get, "/api/v1/health", AuthPolicy::None, false},
        {RouteMethod::Post, "/api/v1/auth/login", AuthPolicy::None, false},
        {RouteMethod::Post, "/api/v1/auth/logout", AuthPolicy::SessionCsrf, false},
        {RouteMethod::Get, "/api/v1/auth/me", AuthPolicy::Session, false},

        {RouteMethod::Get, "/api/v1/users", AuthPolicy::Session, false},
        {RouteMethod::Post, "/api/v1/users", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Patch, "/api/v1/users/{id}", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Post, "/api/v1/users/{id}/archive", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Post, "/api/v1/users/{id}/topup", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Post, "/api/v1/users/{id}/adjust", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Get, "/api/v1/users/{id}/transactions", AuthPolicy::Session, false},

        {RouteMethod::Get, "/api/v1/products", AuthPolicy::Session, false},
        {RouteMethod::Post, "/api/v1/products", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Patch, "/api/v1/products/{id}", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Delete, "/api/v1/products/{id}", AuthPolicy::SessionCsrf, true},

        {RouteMethod::Get, "/api/v1/transactions", AuthPolicy::Session, false},
        {RouteMethod::Post, "/api/v1/transactions/{id}/void", AuthPolicy::SessionCsrf, true},

        {RouteMethod::Get, "/api/v1/expenses", AuthPolicy::Session, false},
        {RouteMethod::Post, "/api/v1/expenses", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Patch, "/api/v1/expenses/{id}", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Post, "/api/v1/expenses/{id}/void", AuthPolicy::SessionCsrf, true},

        {RouteMethod::Get, "/api/v1/cash-counts", AuthPolicy::Session, false},
        {RouteMethod::Post, "/api/v1/cash-counts", AuthPolicy::SessionCsrf, true},

        {RouteMethod::Get, "/api/v1/stats/summary", AuthPolicy::Session, false},
        {RouteMethod::Get, "/api/v1/stats/leaderboard", AuthPolicy::Session, false},
        {RouteMethod::Get, "/api/v1/stats/longterm", AuthPolicy::Session, false},
        {RouteMethod::Get, "/api/v1/reports/{kind}", AuthPolicy::Session, false},

        {RouteMethod::Get, "/api/v1/settings", AuthPolicy::Session, false},
        {RouteMethod::Put, "/api/v1/settings", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Get, "/api/v1/settings/display", AuthPolicy::Session, false},
        {RouteMethod::Put, "/api/v1/settings/display", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Post, "/api/v1/settings/password", AuthPolicy::SessionCsrf, true},

        {RouteMethod::Get, "/api/v1/time", AuthPolicy::Session, false},
        {RouteMethod::Post, "/api/v1/time/sync", AuthPolicy::SessionCsrf, false},

        {RouteMethod::Get, "/api/v1/ap", AuthPolicy::Session, false},
        {RouteMethod::Post, "/api/v1/ap/start", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Post, "/api/v1/ap/stop", AuthPolicy::SessionCsrf, false},
        {RouteMethod::Post, "/api/v1/ap/extend", AuthPolicy::SessionCsrf, false},

        {RouteMethod::Get, "/api/v1/device", AuthPolicy::Session, false},
        {RouteMethod::Post, "/api/v1/display/test", AuthPolicy::SessionCsrf, false},
        {RouteMethod::Get, "/api/v1/logs", AuthPolicy::Session, false},
        {RouteMethod::Get, "/api/v1/backup", AuthPolicy::Session, false},
        {RouteMethod::Post, "/api/v1/restore", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Post, "/api/v1/factory-reset", AuthPolicy::SessionCsrf, true},
        {RouteMethod::Post, "/api/v1/ota", AuthPolicy::SessionCsrf, false},
    };
    return kSpecs;
}

bool route_allowed_when_password_must_change(const RouteSpec& spec) {
    if (spec.auth == AuthPolicy::None) return true;
    if (same(spec.pattern, "/api/v1/auth/me") && spec.method == RouteMethod::Get) return true;
    if (same(spec.pattern, "/api/v1/auth/logout") && spec.method == RouteMethod::Post) return true;
    if (same(spec.pattern, "/api/v1/settings/password") && spec.method == RouteMethod::Post) {
        return true;
    }
    // Browser time sync is harmless and keeps report periods sane even while
    // the admin is still on the forced password-change screen.
    if (same(spec.pattern, "/api/v1/time/sync") && spec.method == RouteMethod::Post) {
        return true;
    }
    // The SPA needs to render the change-password form, which reads settings.
    if (same(spec.pattern, "/api/v1/settings") && spec.method == RouteMethod::Get) return true;
    return false;
}

const char* route_method_name(RouteMethod method) {
    switch (method) {
        case RouteMethod::Get:
            return "GET";
        case RouteMethod::Post:
            return "POST";
        case RouteMethod::Put:
            return "PUT";
        case RouteMethod::Patch:
            return "PATCH";
        case RouteMethod::Delete:
            return "DELETE";
    }
    return "GET";
}

const char* auth_policy_name(AuthPolicy policy) {
    switch (policy) {
        case AuthPolicy::None:
            return "none";
        case AuthPolicy::Session:
            return "session";
        case AuthPolicy::SessionCsrf:
            return "session+csrf";
    }
    return "none";
}

}  // namespace api
}  // namespace kaffee
