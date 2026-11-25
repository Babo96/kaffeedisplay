#pragma once

#include <string>

#include "kaffee/domain.hpp"
#include "kaffee/settings.hpp"

namespace kaffee {

// Portable backup document, identical to `GET /api/v1/backup` /
// `KaffeeService::export_backup()`. Secrets are always excluded through
// `settings_to_json(settings, false)`.
//
// This helper is pure so the SD backup task can build the payload from a
// `KaffeeService::ledger_snapshot()` and a secret-free settings copy without
// holding the service mutex during flash I/O.
std::string backup_to_json(const Ledger& ledger, const Settings& settings, Timestamp created_at);

}  // namespace kaffee
