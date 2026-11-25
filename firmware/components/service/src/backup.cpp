#include "kaffee/backup.hpp"

#include <utility>

#include "kaffee/json.hpp"

namespace kaffee {

std::string backup_to_json(const Ledger& ledger, const Settings& settings, Timestamp created_at) {
    nlohmann::json users = nlohmann::json::array();
    for (const User& user : ledger.users()) users.push_back(user_to_json(user));
    nlohmann::json products = nlohmann::json::array();
    for (const Product& product : ledger.products()) products.push_back(product_to_json(product));
    nlohmann::json events = nlohmann::json::array();
    for (const Event& event : ledger.events()) events.push_back(event_to_json(event));
    nlohmann::json expenses = nlohmann::json::array();
    for (const Expense& expense : ledger.expenses()) expenses.push_back(expense_to_json(expense));
    nlohmann::json cash_counts = nlohmann::json::array();
    for (const CashCount& count : ledger.cash_counts())
        cash_counts.push_back(cash_count_to_json(count));

    const nlohmann::json backup{{"format", "kaffeekasse-backup"},
                                {"version", 1},
                                {"created_at", created_at},
                                {"settings", settings_to_json(settings, false)},
                                {"users", std::move(users)},
                                {"products", std::move(products)},
                                {"events", std::move(events)},
                                {"expenses", std::move(expenses)},
                                {"cash_counts", std::move(cash_counts)}};
    return backup.dump(2);
}

}  // namespace kaffee
