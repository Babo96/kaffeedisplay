#pragma once

#include <nlohmann/json.hpp>

#include "kaffee/domain.hpp"
#include "kaffee/settings.hpp"

namespace kaffee {

nlohmann::json user_to_json(const User& user);
User user_from_json(const nlohmann::json& j);

nlohmann::json product_to_json(const Product& product);
Product product_from_json(const nlohmann::json& j);

nlohmann::json event_to_json(const Event& event);
Event event_from_json(const nlohmann::json& j);

nlohmann::json expense_to_json(const Expense& expense);
Expense expense_from_json(const nlohmann::json& j);

nlohmann::json cash_count_to_json(const CashCount& count);

nlohmann::json summary_to_json(const Summary& summary);
nlohmann::json leaderboard_to_json(const Leaderboard& leaderboard);
nlohmann::json long_term_stats_to_json(const LongTermStats& stats);

nlohmann::json display_settings_to_json(const DisplaySettings& settings);
DisplaySettings display_settings_from_json(const nlohmann::json& j);

nlohmann::json settings_to_json(const Settings& settings, bool include_secrets = false);
Settings settings_from_json(const nlohmann::json& j, const Settings& base = {});

}  // namespace kaffee
