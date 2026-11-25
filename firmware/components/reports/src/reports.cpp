#include "kaffee/reports.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <map>
#include <tuple>

namespace kaffee::reports {
namespace {

// ---------------------------------------------------------------------------
// Calendar helpers (proleptic Gregorian, days since 1970-01-01).
// ---------------------------------------------------------------------------

int64_t floor_div(int64_t value, int64_t divisor) {
    int64_t quotient = value / divisor;
    int64_t remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) --quotient;
    return quotient;
}

int64_t days_from_civil(int year, int month, int day) {
    int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy =
        static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t days, int& year, int& month, int& day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year += (month <= 2 ? 1 : 0);
}

// 0 = Sunday ... 6 = Saturday.
int weekday_from_days(int64_t days) {
    int weekday = static_cast<int>((days + 4) % 7);
    if (weekday < 0) weekday += 7;
    return weekday;
}

int iso_weekday_from_days(int64_t days) {
    int weekday = weekday_from_days(days);
    return weekday == 0 ? 7 : weekday;
}

int64_t last_sunday_days(int year, int month) {
    const int next_year = month == 12 ? year + 1 : year;
    const int next_month = month == 12 ? 1 : month + 1;
    const int64_t last_day = days_from_civil(next_year, next_month, 1) - 1;
    return last_day - weekday_from_days(last_day);
}

Timestamp dst_start(int year) {
    return last_sunday_days(year, 3) * 86400 + 3600;
}

Timestamp dst_end(int year) {
    return last_sunday_days(year, 10) * 86400 + 3600;
}

int utc_year(Timestamp utc) {
    int year = 1970;
    int month = 1;
    int day = 1;
    civil_from_days(floor_div(utc, 86400), year, month, day);
    return year;
}

// Converts a Berlin wall-clock second count (seconds since 1970-01-01 00:00
// local) to UTC. Period boundaries are always at midnight, so the ambiguous
// hour around the autumn transition can never be hit here.
Timestamp wall_to_utc(int64_t wall_seconds) {
    Timestamp candidate = wall_seconds - 3600;
    if (berlin_is_dst(candidate)) candidate = wall_seconds - 7200;
    return candidate;
}

int64_t days_to_midnight_utc(int64_t days) {
    return wall_to_utc(days * 86400);
}

Cents cash_delta(const Ledger& ledger, const Event& event) {
    switch (event.type) {
        case EventType::Topup:
        case EventType::GuestSale:
        case EventType::ExpenseCreated:
        case EventType::ExpenseUpdated:
        case EventType::ExpenseVoided:
            return event.amount_cents;
        case EventType::Void: {
            auto original = ledger.find_event(event.reverses);
            if (!original) return 0;
            if (original->type == EventType::Topup || original->type == EventType::GuestSale)
                return event.amount_cents;
            return 0;
        }
        default:
            return 0;
    }
}

bool in_period(const Period& period, Timestamp ts) {
    return ts >= period.from && ts < period.to;
}

std::string type_label(EventType type) {
    switch (type) {
        case EventType::Purchase:
            return "Einkauf";
        case EventType::GuestSale:
            return "Gastverkauf";
        case EventType::Topup:
            return "Einzahlung";
        case EventType::Adjustment:
            return "Korrektur";
        case EventType::Void:
            return "Storno";
        case EventType::ExpenseCreated:
            return "Ausgabe";
        case EventType::ExpenseUpdated:
            return "Ausgabe geändert";
        case EventType::ExpenseVoided:
            return "Ausgabe storniert";
        case EventType::CashCount:
            return "Kassensturz";
        default:
            return Ledger::event_type_name(type);
    }
}

std::string payment_label(const Event& event) {
    if (event.payment == "balance") return "Guthaben";
    if (event.payment == "cash") {
        if (event.type == EventType::GuestSale) return "Bar (Gast)";
        return "Bar";
    }
    return "";
}

std::string time_quality_label(TimeSource source) {
    switch (source) {
        case TimeSource::Synced:
            return "synchronisiert";
        case TimeSource::Restored:
            return "wiederhergestellt";
        case TimeSource::Unset:
        default:
            return "unbekannt";
    }
}

bool is_financial_event(EventType type) {
    switch (type) {
        case EventType::Purchase:
        case EventType::GuestSale:
        case EventType::Topup:
        case EventType::Adjustment:
        case EventType::Void:
        case EventType::ExpenseCreated:
        case EventType::ExpenseUpdated:
        case EventType::ExpenseVoided:
        case EventType::CashCount:
            return true;
        default:
            return false;
    }
}

bool is_expense_event(EventType type) {
    return type == EventType::ExpenseCreated || type == EventType::ExpenseUpdated ||
           type == EventType::ExpenseVoided;
}

// Balance/cash event types that a `void` event may reverse. User, product,
// settings, audit and cash-count events are never shown as financial rows.
bool is_voidable_financial(EventType type) {
    return type == EventType::Purchase || type == EventType::GuestSale ||
           type == EventType::Topup || type == EventType::Adjustment;
}

Result<Unit> sink_failed() {
    return Result<Unit>::fail("sink", "report sink aborted");
}

bool emit_row(const RowSink& sink, std::initializer_list<Value> values) {
    return sink(std::vector<Value>(values));
}

struct PeriodTotals {
    int member_items = 0;
    Cents member_revenue = 0;
    int guest_items = 0;
    Cents guest_revenue = 0;
    int topup_count = 0;
    Cents topups = 0;
    int expense_count = 0;
    Cents expenses = 0;
    int estimated_events = 0;
    Cents cash_movement = 0;
};

struct ConsumptionAggregate {
    std::string user;
    std::string product;
    std::string category;
    int count = 0;
    Cents amount = 0;
    int corrections = 0;
};

struct DepositAggregate {
    std::string user;
    int count = 0;
    Cents amount = 0;
    int corrections = 0;
};

struct BalanceAggregate {
    std::string user;
    Cents balance = 0;
    bool active = true;
};

std::string consumption_user_label(const ConsumptionAggregate& aggregate, bool guest) {
    return guest ? std::string() : aggregate.user;
}

Result<Unit> report_overview(const Ledger& ledger, const Period& period, const RowSink& sink) {
    PeriodTotals totals;
    for (const Event& event : ledger.events()) {
        if (!in_period(period, event.ts)) continue;
        totals.cash_movement += cash_delta(ledger, event);
        if (is_financial_event(event.type) && event.time_source != TimeSource::Synced)
            totals.estimated_events += 1;

        // Booking aggregates use original-period attribution: a void removes
        // the original booking from the period in which it was made, matching
        // Ledger::leaderboard(). Cash rows above still follow the void's own
        // timestamp because that is when the money moved.
        switch (event.type) {
            case EventType::Purchase:
                if (ledger.is_voided(event.id)) break;
                totals.member_items += 1;
                totals.member_revenue += -event.amount_cents;
                break;
            case EventType::GuestSale:
                if (ledger.is_voided(event.id)) break;
                totals.guest_items += 1;
                totals.guest_revenue += event.amount_cents;
                break;
            case EventType::Topup:
                if (ledger.is_voided(event.id)) break;
                totals.topup_count += 1;
                totals.topups += event.amount_cents;
                break;
            case EventType::ExpenseCreated:
                if (ledger.is_voided(event.expense_id)) break;
                totals.expense_count += 1;
                totals.expenses += -event.amount_cents;
                break;
            case EventType::ExpenseUpdated:
                if (ledger.is_voided(event.expense_id)) break;
                totals.expenses += -event.amount_cents;
                break;
            case EventType::ExpenseVoided:
            case EventType::Void:
                // Reversals are attributed to the original booking, which is
                // excluded above while it is voided.
                break;
            default:
                break;
        }
    }

    Cents opening_cash = 0;
    for (const Event& event : ledger.events()) {
        if (event.ts < period.from) opening_cash += cash_delta(ledger, event);
    }
    const Cents closing_cash = opening_cash + totals.cash_movement;

    std::map<std::string, Cents> balances;
    for (const Event& event : ledger.events()) {
        if (event.ts >= period.to || event.user_id.empty()) continue;
        balances[event.user_id] += event.amount_cents;
    }
    Cents prepaid = 0;
    Cents debts = 0;
    for (const auto& entry : balances) {
        if (entry.second > 0)
            prepaid += entry.second;
        else if (entry.second < 0)
            debts += -entry.second;
    }
    const Cents net_liabilities = prepaid - debts;
    const Cents equity = closing_cash - net_liabilities;

    struct Row {
        const char* metric;
        int64_t count;
        Cents amount;
    };
    const Row rows[] = {
        {"Mitglieder: Artikel", totals.member_items, totals.member_revenue},
        {"Gastverkäufe: Artikel", totals.guest_items, totals.guest_revenue},
        {"Einzahlungen", totals.topup_count, totals.topups},
        {"Ausgaben (Belege)", totals.expense_count, totals.expenses},
        {"Geschätzte Zeitangaben", totals.estimated_events, 0},
        {"Anfangssaldo Kasse", 0, opening_cash},
        {"Endsaldo Kasse", 0, closing_cash},
        {"Guthaben (Forderungen)", 0, prepaid},
        {"Schulden (Verbindlichkeiten)", 0, debts},
        {"Nettoverbindlichkeiten", 0, net_liabilities},
        {"Eigenkapital", 0, equity},
    };
    for (const Row& row : rows) {
        if (!emit_row(sink, {Value::of_text(row.metric), Value::integer(row.count),
                             Value::money(row.amount), Value::money(row.amount)}))
            return sink_failed();
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> report_consumption(const Ledger& ledger, const Period& period, const RowSink& sink) {
    std::map<std::tuple<int, std::string, std::string>, ConsumptionAggregate> aggregates;
    for (const Event& event : ledger.events()) {
        if (!in_period(period, event.ts)) continue;
        if (event.type == EventType::Purchase) {
            auto& aggregate = aggregates[{0, event.user_id, event.product_id}];
            aggregate.user = event.user_name;
            aggregate.product = event.product_name;
            aggregate.category = event.product_category;
            if (ledger.is_voided(event.id)) {
                // Void attribution: the correction belongs to the original
                // booking period, so the booking is netted out here even when
                // the reversal happened later. Keep a marker for transparency.
                aggregate.corrections += 1;
            } else {
                aggregate.count += 1;
                aggregate.amount += -event.amount_cents;
            }
        } else if (event.type == EventType::GuestSale) {
            auto& aggregate = aggregates[{1, "", event.product_id}];
            aggregate.product = event.product_name;
            aggregate.category = event.product_category;
            if (ledger.is_voided(event.id)) {
                aggregate.corrections += 1;
            } else {
                aggregate.count += 1;
                aggregate.amount += event.amount_cents;
            }
        }
        // Void events are not rows here: their effect is applied to the
        // original booking above.
    }

    for (const auto& entry : aggregates) {
        const bool guest = std::get<0>(entry.first) == 1;
        const ConsumptionAggregate& aggregate = entry.second;
        if (!emit_row(sink,
                      {Value::of_text(guest ? "Gast" : "Mitglied"),
                       Value::of_text(consumption_user_label(aggregate, guest)),
                       Value::of_text(aggregate.product), Value::of_text(aggregate.category),
                       Value::integer(aggregate.count), Value::money(aggregate.amount),
                       Value::money(aggregate.amount), Value::integer(aggregate.corrections)}))
            return sink_failed();
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> report_deposits(const Ledger& ledger, const Period& period, const RowSink& sink) {
    std::map<std::string, DepositAggregate> aggregates;
    for (const Event& event : ledger.events()) {
        if (!in_period(period, event.ts)) continue;
        if (event.type == EventType::Topup) {
            auto& aggregate = aggregates[event.user_id];
            aggregate.user = event.user_name;
            aggregate.count += 1;
            aggregate.amount += event.amount_cents;
        } else if (event.type == EventType::Void) {
            auto original = ledger.find_event(event.reverses);
            if (!original || original->type != EventType::Topup) continue;
            auto& aggregate = aggregates[event.user_id];
            aggregate.user = event.user_name;
            aggregate.count -= 1;
            aggregate.amount += event.amount_cents;
            aggregate.corrections += 1;
        }
    }

    std::vector<DepositAggregate> rows;
    rows.reserve(aggregates.size());
    for (const auto& entry : aggregates) rows.push_back(entry.second);
    std::sort(rows.begin(), rows.end(), [](const DepositAggregate& a, const DepositAggregate& b) {
        if (a.user != b.user) return a.user < b.user;
        return false;
    });
    for (const DepositAggregate& row : rows) {
        if (!emit_row(sink, {Value::of_text(row.user), Value::integer(row.count),
                             Value::money(row.amount), Value::money(row.amount),
                             Value::integer(row.corrections)}))
            return sink_failed();
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> report_expenses(const Ledger& ledger, const Period& period, const RowSink& sink) {
    for (const Event& event : ledger.events()) {
        if (!in_period(period, event.ts) || !is_expense_event(event.type)) continue;
        std::string status = "erfasst";
        if (event.type == EventType::ExpenseVoided)
            status = "Storno";
        else if (ledger.is_voided(event.expense_id))
            status = "storniert";
        else if (event.type == EventType::ExpenseUpdated)
            status = "geändert";
        if (!emit_row(sink, {Value::datetime(event.ts), Value::of_text(event.expense_category),
                             Value::of_text(event.expense_description),
                             Value::of_text(event.expense_vendor), Value::money(event.amount_cents),
                             Value::money(event.amount_cents), Value::of_text(status),
                             Value::of_text(event.note),
                             Value::of_text(time_quality_label(event.time_source))}))
            return sink_failed();
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> report_cashflow(const Ledger& ledger, const Period& period, const RowSink& sink) {
    Cents opening = 0;
    Cents deposits = 0;
    Cents guest_sales = 0;
    Cents expenses = 0;
    Cents movement = 0;
    for (const Event& event : ledger.events()) {
        const Cents delta = cash_delta(ledger, event);
        if (event.ts < period.from) {
            opening += delta;
            continue;
        }
        if (!in_period(period, event.ts)) continue;
        movement += delta;
        switch (event.type) {
            case EventType::Topup:
                deposits += delta;
                break;
            case EventType::GuestSale:
                guest_sales += delta;
                break;
            case EventType::ExpenseCreated:
            case EventType::ExpenseUpdated:
            case EventType::ExpenseVoided:
                expenses += delta;
                break;
            case EventType::Void: {
                auto original = ledger.find_event(event.reverses);
                if (!original) break;
                if (original->type == EventType::Topup)
                    deposits += delta;
                else if (original->type == EventType::GuestSale)
                    guest_sales += delta;
                break;
            }
            default:
                break;
        }
    }
    const Cents closing = opening + movement;

    struct Row {
        const char* position;
        Cents amount;
        const char* note;
    };
    const Row rows[] = {
        {"Anfangssaldo Kasse", opening, "Bestand vor dem Zeitraum"},
        {"Einzahlungen (Mitglieder)", deposits, "Barzahlungen minus Stornos"},
        {"Gastverkäufe", guest_sales, "Sofortzahlungen minus Stornos"},
        {"Ausgaben", expenses, "Belege minus Stornos"},
        {"Endsaldo Kasse", closing, "Anfangssaldo plus Bewegungen"},
    };
    for (const Row& row : rows) {
        if (!emit_row(sink, {Value::of_text(row.position), Value::money(row.amount),
                             Value::money(row.amount), Value::of_text(row.note)}))
            return sink_failed();
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> report_balances(const Ledger& ledger, const Period& period, const RowSink& sink) {
    // Reconstruct user names and active status as of `period.to` from the
    // event snapshots. The current `User::active` flag is not used because a
    // user archived later must still appear active in an earlier report.
    struct UserState {
        std::string name;
        bool active = true;
    };
    std::map<std::string, UserState> users;
    std::map<std::string, Cents> balances;
    for (const Event& event : ledger.events()) {
        if (event.ts >= period.to) continue;
        if (!event.user_id.empty()) balances[event.user_id] += event.amount_cents;
        switch (event.type) {
            case EventType::UserCreated:
            case EventType::UserUpdated:
            case EventType::UserArchived: {
                auto& state = users[event.user_id];
                state.name = event.user_name;
                state.active = event.user_active;
                break;
            }
            default:
                break;
        }
    }
    // Defensive fallback for ledgers without user snapshots; honor the
    // creation timestamp so users created after the period are never listed.
    for (const User& user : ledger.users()) {
        if (user.created_at >= period.to) continue;
        auto it = users.find(user.id);
        if (it == users.end()) users[user.id] = UserState{user.name, user.active};
    }

    std::vector<BalanceAggregate> rows;
    rows.reserve(users.size());
    for (const auto& entry : users) {
        BalanceAggregate aggregate;
        aggregate.user = entry.second.name;
        aggregate.active = entry.second.active;
        auto it = balances.find(entry.first);
        if (it != balances.end()) aggregate.balance = it->second;
        rows.push_back(std::move(aggregate));
    }
    std::sort(rows.begin(), rows.end(),
              [](const BalanceAggregate& a, const BalanceAggregate& b) { return a.user < b.user; });

    Cents total_balance = 0;
    Cents total_prepaid = 0;
    Cents total_debt = 0;
    for (const BalanceAggregate& row : rows) {
        const Cents prepaid = row.balance > 0 ? row.balance : 0;
        const Cents debt = row.balance < 0 ? -row.balance : 0;
        total_balance += row.balance;
        total_prepaid += prepaid;
        total_debt += debt;
        if (!emit_row(sink, {Value::of_text(row.user), Value::money(row.balance),
                             Value::money(row.balance), Value::money(prepaid),
                             Value::money(prepaid), Value::money(debt), Value::money(debt),
                             Value::of_text(row.active ? "aktiv" : "archiviert")}))
            return sink_failed();
    }
    if (!emit_row(sink, {Value::of_text("Summe"), Value::money(total_balance),
                         Value::money(total_balance), Value::money(total_prepaid),
                         Value::money(total_prepaid), Value::money(total_debt),
                         Value::money(total_debt), Value::of_text("")}))
        return sink_failed();
    return Result<Unit>::ok(Unit{});
}

Result<Unit> report_transactions(const Ledger& ledger, const Period& period, const RowSink& sink) {
    for (const Event& event : ledger.events()) {
        if (!in_period(period, event.ts)) continue;
        if (event.type == EventType::Void) {
            // A void is only a financial row when it reverses a booking.
            // Voids of user/product/audit events are hidden.
            auto original = ledger.find_event(event.reverses);
            if (!original || !is_voidable_financial(original->type)) continue;
        } else if (!is_financial_event(event.type)) {
            continue;
        }

        std::string status = "gebucht";
        if (event.type == EventType::Void)
            status = "Storno";
        else if (is_expense_event(event.type)) {
            if (ledger.is_voided(event.expense_id)) status = "storniert";
            if (event.type == EventType::ExpenseVoided) status = "Storno";
        } else if (ledger.is_voided(event.id)) {
            status = "storniert";
        }

        std::string description;
        if (is_expense_event(event.type))
            description = event.expense_description;
        else if (event.type == EventType::CashCount)
            description = "Kassenzählung";
        else
            description = event.product_name;

        std::string note = event.note;
        if (event.type == EventType::Void && !event.reverses.empty())
            note = note.empty() ? "Storno zu " + event.reverses
                                : note + " (zu " + event.reverses + ")";

        if (!emit_row(sink, {Value::datetime(event.ts), Value::of_text(type_label(event.type)),
                             Value::of_text(event.user_name), Value::of_text(event.product_name),
                             Value::of_text(description), Value::of_text(payment_label(event)),
                             Value::money(event.amount_cents), Value::money(event.amount_cents),
                             Value::of_text(status),
                             Value::of_text(time_quality_label(event.time_source)),
                             Value::of_text(note)}))
            return sink_failed();
    }
    return Result<Unit>::ok(Unit{});
}

}  // namespace

// ---------------------------------------------------------------------------
// Period helpers
// ---------------------------------------------------------------------------

bool berlin_is_dst(Timestamp utc) {
    const int year = utc_year(utc);
    return utc >= dst_start(year) && utc < dst_end(year);
}

int berlin_utc_offset(Timestamp utc) {
    return berlin_is_dst(utc) ? 7200 : 3600;
}

Civil to_berlin(Timestamp utc) {
    const Timestamp local = utc + berlin_utc_offset(utc);
    const int64_t days = floor_div(local, 86400);
    int64_t seconds = local - days * 86400;

    Civil civil;
    civil_from_days(days, civil.year, civil.month, civil.day);
    civil.hour = static_cast<int>(seconds / 3600);
    seconds %= 3600;
    civil.minute = static_cast<int>(seconds / 60);
    civil.second = static_cast<int>(seconds % 60);
    civil.weekday = iso_weekday_from_days(days);
    return civil;
}

Timestamp from_berlin(const Civil& local) {
    const int64_t wall = days_from_civil(local.year, local.month, local.day) * 86400 +
                         static_cast<int64_t>(local.hour) * 3600 +
                         static_cast<int64_t>(local.minute) * 60 + local.second;
    return wall_to_utc(wall);
}

Period week(Timestamp now) {
    const Civil civil = to_berlin(now);
    int64_t start_days = days_from_civil(civil.year, civil.month, civil.day);
    start_days -= civil.weekday - 1;
    Period p;
    p.from = days_to_midnight_utc(start_days);
    p.to = days_to_midnight_utc(start_days + 7);
    p.kind = PeriodKind::Week;
    return p;
}

Period month(Timestamp now) {
    const Civil civil = to_berlin(now);
    const int next_year = civil.month == 12 ? civil.year + 1 : civil.year;
    const int next_month = civil.month == 12 ? 1 : civil.month + 1;
    Period p;
    p.from = days_to_midnight_utc(days_from_civil(civil.year, civil.month, 1));
    p.to = days_to_midnight_utc(days_from_civil(next_year, next_month, 1));
    p.kind = PeriodKind::Month;
    return p;
}

Period year(Timestamp now) {
    const Civil civil = to_berlin(now);
    Period p;
    p.from = days_to_midnight_utc(days_from_civil(civil.year, 1, 1));
    p.to = days_to_midnight_utc(days_from_civil(civil.year + 1, 1, 1));
    p.kind = PeriodKind::Year;
    return p;
}

Period all() {
    Period p;
    p.from = 0;
    p.to = std::numeric_limits<Timestamp>::max();
    p.kind = PeriodKind::All;
    return p;
}

Period custom(Timestamp from, Timestamp to) {
    Period p;
    p.from = from;
    p.to = to;
    p.kind = PeriodKind::Custom;
    return p;
}

Period period(PeriodKind kind, Timestamp now) {
    switch (kind) {
        case PeriodKind::Week:
            return week(now);
        case PeriodKind::Month:
            return month(now);
        case PeriodKind::Year:
            return year(now);
        case PeriodKind::All:
            return all();
        case PeriodKind::Custom:
        default:
            return Period{};
    }
}

bool contains(const Period& p, Timestamp ts) {
    return ts >= p.from && ts < p.to;
}

bool valid(const Period& p) {
    return p.to > p.from;
}

// ---------------------------------------------------------------------------
// Values and formatting
// ---------------------------------------------------------------------------

Value Value::of_text(std::string value) {
    Value v;
    v.text = std::move(value);
    return v;
}

Value Value::money(Cents value) {
    Value v;
    v.cents = value;
    return v;
}

Value Value::date(Timestamp value) {
    Value v;
    v.ts = value;
    return v;
}

Value Value::datetime(Timestamp value) {
    Value v;
    v.ts = value;
    return v;
}

Value Value::integer(int64_t value) {
    Value v;
    v.number = value;
    return v;
}

Value Value::boolean(bool value) {
    Value v;
    v.flag = value;
    return v;
}

std::string format_money(Cents cents, CsvFormat format) {
    const bool negative = cents < 0;
    const uint64_t magnitude =
        negative ? static_cast<uint64_t>(-(cents + 1)) + 1 : static_cast<uint64_t>(cents);
    const char separator = format == CsvFormat::Excel ? ',' : '.';
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%s%llu%c%02llu", negative ? "-" : "",
                  static_cast<unsigned long long>(magnitude / 100), separator,
                  static_cast<unsigned long long>(magnitude % 100));
    return buffer;
}

std::string format_date(Timestamp ts, CsvFormat format) {
    const Civil civil = to_berlin(ts);
    char buffer[32];
    if (format == CsvFormat::Excel) {
        std::snprintf(buffer, sizeof(buffer), "%02d.%02d.%04d", civil.day, civil.month, civil.year);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", civil.year, civil.month, civil.day);
    }
    return buffer;
}

std::string format_datetime(Timestamp ts, CsvFormat format) {
    const Civil civil = to_berlin(ts);
    char buffer[48];
    if (format == CsvFormat::Excel) {
        std::snprintf(buffer, sizeof(buffer), "%02d.%02d.%04d %02d:%02d:%02d", civil.day,
                      civil.month, civil.year, civil.hour, civil.minute, civil.second);
    } else {
        const int offset = berlin_utc_offset(ts);
        const bool positive = offset >= 0;
        const int absolute = positive ? offset : -offset;
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                      civil.year, civil.month, civil.day, civil.hour, civil.minute, civil.second,
                      positive ? '+' : '-', absolute / 3600, (absolute % 3600) / 60);
    }
    return buffer;
}

std::string escape_field(std::string_view value, CsvFormat format) {
    const char delimiter = format == CsvFormat::Excel ? ';' : ',';
    const bool needs_quotes = value.find(delimiter) != std::string_view::npos ||
                              value.find('"') != std::string_view::npos ||
                              value.find('\n') != std::string_view::npos ||
                              value.find('\r') != std::string_view::npos;
    if (!needs_quotes) return std::string(value);

    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

namespace {

bool looks_numeric(std::string_view value) {
    if (value.empty()) return false;
    size_t i = 0;
    if (value[i] == '+' || value[i] == '-') ++i;
    bool digits = false;
    while (i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))) {
        ++i;
        digits = true;
    }
    if (i < value.size() && (value[i] == '.' || value[i] == ',')) {
        ++i;
        while (i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))) {
            ++i;
            digits = true;
        }
    }
    if (!digits) return false;
    if (i < value.size() && (value[i] == 'e' || value[i] == 'E')) {
        ++i;
        if (i < value.size() && (value[i] == '+' || value[i] == '-')) ++i;
        bool exponent_digits = false;
        while (i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))) {
            ++i;
            exponent_digits = true;
        }
        if (!exponent_digits) return false;
    }
    return i == value.size();
}

}  // namespace

std::string protect_formula(std::string_view value) {
    if (value.empty()) return std::string();
    const char first = value.front();
    if (first != '=' && first != '+' && first != '-' && first != '@' && first != '\t' &&
        first != '\r')
        return std::string(value);
    if (looks_numeric(value)) return std::string(value);
    std::string out;
    out.reserve(value.size() + 1);
    out.push_back('\'');
    out.append(value);
    return out;
}

// ---------------------------------------------------------------------------
// CSV writer
// ---------------------------------------------------------------------------

CsvWriter::CsvWriter(CsvFormat format, Sink sink) : format_(format), sink_(std::move(sink)) {}

bool CsvWriter::emit(std::string_view chunk) {
    if (!ok_) return false;
    if (!sink_.write || !sink_.write(chunk)) {
        ok_ = false;
        return false;
    }
    return true;
}

bool CsvWriter::begin() {
    if (began_ || !ok_) return false;
    began_ = true;
    if (format_ == CsvFormat::Excel) return emit("\xEF\xBB\xBF") && emit("sep=;\r\n");
    return true;
}

bool CsvWriter::header(const std::vector<Column>& columns) {
    if (!began_) return false;
    std::string line;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i != 0) line.push_back(format_ == CsvFormat::Excel ? ';' : ',');
        const std::string_view name =
            format_ == CsvFormat::Excel ? columns[i].label : columns[i].key;
        line += escape_field(name, format_);
    }
    line += format_ == CsvFormat::Excel ? "\r\n" : "\n";
    return emit(line);
}

bool CsvWriter::row(const std::vector<Column>& columns, const std::vector<Value>& values) {
    if (!began_ || columns.size() != values.size()) {
        ok_ = false;
        return false;
    }
    std::string line;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i != 0) line.push_back(format_ == CsvFormat::Excel ? ';' : ',');
        std::string field;
        const Value& value = values[i];
        switch (columns[i].type) {
            case ColumnType::Text:
                field = format_ == CsvFormat::Excel ? protect_formula(value.text) : value.text;
                break;
            case ColumnType::MoneyEur:
                field = format_money(value.cents, format_);
                break;
            case ColumnType::MoneyCents:
                field = std::to_string(value.cents);
                break;
            case ColumnType::Date:
                field = format_date(value.ts, format_);
                break;
            case ColumnType::DateTime:
                field = format_datetime(value.ts, format_);
                break;
            case ColumnType::Integer:
                field = std::to_string(value.number);
                break;
            case ColumnType::Bool:
                if (format_ == CsvFormat::Excel)
                    field = value.flag ? "ja" : "nein";
                else
                    field = value.flag ? "true" : "false";
                break;
        }
        line += escape_field(field, format_);
    }
    line += format_ == CsvFormat::Excel ? "\r\n" : "\n";
    return emit(line);
}

bool CsvWriter::end() {
    return ok_;
}

// ---------------------------------------------------------------------------
// Report tables
// ---------------------------------------------------------------------------

const std::vector<Column>& columns(ReportKind kind) {
    static const std::vector<Column> overview = {
        {"metric", "Kennzahl", ColumnType::Text},
        {"count", "Anzahl", ColumnType::Integer},
        {"amount_eur", "Betrag (EUR)", ColumnType::MoneyEur},
        {"amount_cents", "Betrag (Cent)", ColumnType::MoneyCents},
    };
    static const std::vector<Column> consumption = {
        {"kind", "Art", ColumnType::Text},
        {"user", "Mitglied", ColumnType::Text},
        {"product", "Produkt", ColumnType::Text},
        {"category", "Kategorie", ColumnType::Text},
        {"count", "Anzahl", ColumnType::Integer},
        {"amount_eur", "Betrag (EUR)", ColumnType::MoneyEur},
        {"amount_cents", "Betrag (Cent)", ColumnType::MoneyCents},
        {"corrections", "Stornos", ColumnType::Integer},
    };
    static const std::vector<Column> deposits = {
        {"user", "Mitglied", ColumnType::Text},
        {"count", "Anzahl", ColumnType::Integer},
        {"amount_eur", "Betrag (EUR)", ColumnType::MoneyEur},
        {"amount_cents", "Betrag (Cent)", ColumnType::MoneyCents},
        {"corrections", "Stornos", ColumnType::Integer},
    };
    static const std::vector<Column> expenses = {
        {"timestamp", "Zeitpunkt", ColumnType::DateTime},
        {"category", "Kategorie", ColumnType::Text},
        {"description", "Beschreibung", ColumnType::Text},
        {"vendor", "Lieferant", ColumnType::Text},
        {"amount_eur", "Betrag (EUR)", ColumnType::MoneyEur},
        {"amount_cents", "Betrag (Cent)", ColumnType::MoneyCents},
        {"status", "Status", ColumnType::Text},
        {"note", "Notiz", ColumnType::Text},
        {"time_quality", "Zeitqualität", ColumnType::Text},
    };
    static const std::vector<Column> cashflow = {
        {"position", "Position", ColumnType::Text},
        {"amount_eur", "Betrag (EUR)", ColumnType::MoneyEur},
        {"amount_cents", "Betrag (Cent)", ColumnType::MoneyCents},
        {"note", "Hinweis", ColumnType::Text},
    };
    static const std::vector<Column> balances = {
        {"user", "Mitglied", ColumnType::Text},
        {"balance_eur", "Saldo (EUR)", ColumnType::MoneyEur},
        {"balance_cents", "Saldo (Cent)", ColumnType::MoneyCents},
        {"prepaid_eur", "Guthaben (EUR)", ColumnType::MoneyEur},
        {"prepaid_cents", "Guthaben (Cent)", ColumnType::MoneyCents},
        {"debt_eur", "Schulden (EUR)", ColumnType::MoneyEur},
        {"debt_cents", "Schulden (Cent)", ColumnType::MoneyCents},
        {"status", "Status", ColumnType::Text},
    };
    static const std::vector<Column> transactions = {
        {"timestamp", "Zeitpunkt", ColumnType::DateTime},
        {"type", "Typ", ColumnType::Text},
        {"user", "Mitglied", ColumnType::Text},
        {"product", "Produkt", ColumnType::Text},
        {"description", "Beschreibung", ColumnType::Text},
        {"payment", "Zahlungsart", ColumnType::Text},
        {"amount_eur", "Betrag (EUR)", ColumnType::MoneyEur},
        {"amount_cents", "Betrag (Cent)", ColumnType::MoneyCents},
        {"status", "Status", ColumnType::Text},
        {"time_quality", "Zeitqualität", ColumnType::Text},
        {"note", "Notiz", ColumnType::Text},
    };
    static const std::vector<Column> empty;

    switch (kind) {
        case ReportKind::Overview:
            return overview;
        case ReportKind::Consumption:
            return consumption;
        case ReportKind::Deposits:
            return deposits;
        case ReportKind::Expenses:
            return expenses;
        case ReportKind::Cashflow:
            return cashflow;
        case ReportKind::Balances:
            return balances;
        case ReportKind::Transactions:
            return transactions;
    }
    return empty;
}

Result<Unit> for_each_row(const Ledger& ledger, ReportKind kind, const Period& p,
                          const RowSink& sink) {
    if (!sink) return Result<Unit>::fail("sink", "missing row sink");
    if (!valid(p)) return Result<Unit>::fail("period", "period end must be after period start");
    if (columns(kind).empty()) return Result<Unit>::fail("kind", "unknown report kind");

    switch (kind) {
        case ReportKind::Overview:
            return report_overview(ledger, p, sink);
        case ReportKind::Consumption:
            return report_consumption(ledger, p, sink);
        case ReportKind::Deposits:
            return report_deposits(ledger, p, sink);
        case ReportKind::Expenses:
            return report_expenses(ledger, p, sink);
        case ReportKind::Cashflow:
            return report_cashflow(ledger, p, sink);
        case ReportKind::Balances:
            return report_balances(ledger, p, sink);
        case ReportKind::Transactions:
            return report_transactions(ledger, p, sink);
    }
    return Result<Unit>::fail("kind", "unknown report kind");
}

Result<Unit> write_csv(const Ledger& ledger, ReportKind kind, const Period& p, CsvFormat format,
                       const Sink& sink) {
    if (!sink.write) return Result<Unit>::fail("sink", "missing sink");
    if (!valid(p)) return Result<Unit>::fail("period", "period end must be after period start");
    const std::vector<Column>& table = columns(kind);
    if (table.empty()) return Result<Unit>::fail("kind", "unknown report kind");

    CsvWriter writer(format, sink);
    if (!writer.begin() || !writer.header(table))
        return Result<Unit>::fail("sink", "report sink aborted");

    auto result = for_each_row(
        ledger, kind, p, [&](const std::vector<Value>& row) { return writer.row(table, row); });
    if (!result) return result;
    if (!writer.end()) return Result<Unit>::fail("sink", "report sink aborted");
    return Result<Unit>::ok(Unit{});
}

// ---------------------------------------------------------------------------
// JSON envelope
// ---------------------------------------------------------------------------

namespace {

const char* column_type_name(ColumnType type) {
    switch (type) {
        case ColumnType::MoneyEur:
        case ColumnType::MoneyCents:
            return "money";
        case ColumnType::Date:
            return "date";
        case ColumnType::DateTime:
            return "datetime";
        case ColumnType::Integer:
            return "number";
        case ColumnType::Bool:
            return "boolean";
        case ColumnType::Text:
            break;
    }
    return "text";
}

nlohmann::json value_json(ColumnType type, const Value& value) {
    switch (type) {
        case ColumnType::MoneyEur:
        case ColumnType::MoneyCents:
            return value.cents;
        case ColumnType::Date:
        case ColumnType::DateTime:
            return value.ts;
        case ColumnType::Integer:
            return value.number;
        case ColumnType::Bool:
            return value.flag;
        case ColumnType::Text:
            break;
    }
    return value.text;
}

}  // namespace

nlohmann::json to_json(const Ledger& ledger, ReportKind kind, const Period& p) {
    nlohmann::json out{{"kind", report_kind_name(kind)}, {"period", period_kind_name(p.kind)}};
    // All-time reports have no meaningful boundaries. The SPA treats missing
    // from/to as "all time"; INT64_MAX would overflow JavaScript Date.
    if (p.kind != PeriodKind::All) {
        out["from"] = p.from;
        out["to"] = p.to;
    }

    const std::vector<Column>& table = columns(kind);
    nlohmann::json column_json = nlohmann::json::array();
    for (const Column& column : table) {
        column_json.push_back({{"key", std::string(column.key)},
                               {"label", std::string(column.label)},
                               {"type", column_type_name(column.type)}});
    }
    out["columns"] = std::move(column_json);

    nlohmann::json rows = nlohmann::json::array();
    auto result = for_each_row(ledger, kind, p, [&](const std::vector<Value>& values) {
        nlohmann::json row = nlohmann::json::object();
        const size_t count = std::min(table.size(), values.size());
        for (size_t i = 0; i < count; ++i) {
            row[std::string(table[i].key)] = value_json(table[i].type, values[i]);
        }
        rows.push_back(std::move(row));
        return true;
    });
    if (!result) {
        // The sink never aborts and the period is validated by the caller; keep
        // the envelope shape even if a future report returns an error.
        rows = nlohmann::json::array();
    }
    out["rows"] = std::move(rows);
    return out;
}

// ---------------------------------------------------------------------------
// Names, content type and filename
// ---------------------------------------------------------------------------

std::string report_kind_name(ReportKind kind) {
    switch (kind) {
        case ReportKind::Overview:
            return "overview";
        case ReportKind::Consumption:
            return "consumption";
        case ReportKind::Deposits:
            return "deposits";
        case ReportKind::Expenses:
            return "expenses";
        case ReportKind::Cashflow:
            return "cashflow";
        case ReportKind::Balances:
            return "balances";
        case ReportKind::Transactions:
            return "transactions";
    }
    return "unknown";
}

std::optional<ReportKind> report_kind_from_name(std::string_view name) {
    if (name == "overview") return ReportKind::Overview;
    if (name == "consumption") return ReportKind::Consumption;
    if (name == "deposits") return ReportKind::Deposits;
    if (name == "expenses") return ReportKind::Expenses;
    if (name == "cashflow") return ReportKind::Cashflow;
    if (name == "balances") return ReportKind::Balances;
    if (name == "transactions") return ReportKind::Transactions;
    return std::nullopt;
}

std::string period_kind_name(PeriodKind kind) {
    switch (kind) {
        case PeriodKind::Week:
            return "week";
        case PeriodKind::Month:
            return "month";
        case PeriodKind::Year:
            return "year";
        case PeriodKind::All:
            return "all";
        case PeriodKind::Custom:
            return "custom";
    }
    return "custom";
}

std::optional<PeriodKind> period_kind_from_name(std::string_view name) {
    if (name == "week") return PeriodKind::Week;
    if (name == "month") return PeriodKind::Month;
    if (name == "year") return PeriodKind::Year;
    if (name == "all") return PeriodKind::All;
    if (name == "custom") return PeriodKind::Custom;
    return std::nullopt;
}

const char* csv_content_type(CsvFormat format) {
    (void)format;
    return "text/csv; charset=utf-8";
}

namespace {

void iso_week_of(Timestamp ts, int& year, int& week) {
    const Civil civil = to_berlin(ts);
    const int64_t days = days_from_civil(civil.year, civil.month, civil.day);
    const int64_t thursday = days + (4 - iso_weekday_from_days(days));
    int thursday_year = 1970;
    int thursday_month = 1;
    int thursday_day = 1;
    civil_from_days(thursday, thursday_year, thursday_month, thursday_day);
    (void)thursday_month;
    (void)thursday_day;
    year = thursday_year;
    week = static_cast<int>((thursday - days_from_civil(thursday_year, 1, 1)) / 7 + 1);
}

}  // namespace

std::string report_filename(ReportKind kind, const Period& p) {
    const std::string name = report_kind_name(kind);
    char prefix[64];
    if (p.kind == PeriodKind::All) {
        std::snprintf(prefix, sizeof(prefix), "kaffeekasse_all");
    } else if (p.kind == PeriodKind::Custom) {
        const Civil from = to_berlin(p.from);
        if (p.to == std::numeric_limits<Timestamp>::max()) {
            std::snprintf(prefix, sizeof(prefix), "kaffeekasse_%04d-%02d-%02d_offen", from.year,
                          from.month, from.day);
        } else if (p.to <= p.from) {
            std::snprintf(prefix, sizeof(prefix), "kaffeekasse_%04d-%02d-%02d", from.year,
                          from.month, from.day);
        } else {
            const Civil to = to_berlin(p.to - 1);
            std::snprintf(prefix, sizeof(prefix), "kaffeekasse_%04d-%02d-%02d_%04d-%02d-%02d",
                          from.year, from.month, from.day, to.year, to.month, to.day);
        }
    } else if (p.kind == PeriodKind::Week) {
        int year = 1970;
        int week = 1;
        iso_week_of(p.from, year, week);
        std::snprintf(prefix, sizeof(prefix), "kaffeekasse_%04d-W%02d", year, week);
    } else if (p.kind == PeriodKind::Year) {
        const Civil from = to_berlin(p.from);
        std::snprintf(prefix, sizeof(prefix), "kaffeekasse_%04d", from.year);
    } else {
        const Civil from = to_berlin(p.from);
        std::snprintf(prefix, sizeof(prefix), "kaffeekasse_%04d-%02d", from.year, from.month);
    }
    return std::string(prefix) + "_" + name + ".csv";
}

Leaderboard leaderboard(const Ledger& ledger, const Period& p) {
    return ledger.leaderboard(p.from, p.to);
}

Leaderboard monthly_leaderboard(const Ledger& ledger, Timestamp now) {
    return leaderboard(ledger, month(now));
}

std::vector<LongTermMonth> long_term_months(const Ledger& ledger) {
    std::map<std::pair<int, int>, LongTermMonth> months;
    auto apply = [&](const Event& sale, int sign) {
        const Civil civil = to_berlin(sale.ts);
        LongTermMonth& row = months[{civil.year, civil.month}];
        row.year = civil.year;
        row.month = civil.month;
        if (sale.type == EventType::Purchase) {
            row.member_items += sign;
            row.member_spend_cents += sign * (-sale.amount_cents);
        } else {
            row.guest_items += sign;
            row.guest_spend_cents += sign * sale.amount_cents;
        }
    };

    for (const Event& e : ledger.events()) {
        if (e.type == EventType::Purchase || e.type == EventType::GuestSale) {
            apply(e, 1);
        } else if (e.type == EventType::Void) {
            const auto original = ledger.find_event(e.reverses);
            if (!original) continue;
            if (original->type == EventType::Purchase || original->type == EventType::GuestSale) {
                apply(*original, -1);
            }
        }
    }

    std::vector<LongTermMonth> out;
    out.reserve(months.size());
    for (auto& [key, row] : months) {
        (void)key;
        out.push_back(row);
    }
    std::sort(out.begin(), out.end(), [](const LongTermMonth& a, const LongTermMonth& b) {
        if (a.year != b.year) return a.year < b.year;
        return a.month < b.month;
    });
    return out;
}

}  // namespace kaffee::reports
