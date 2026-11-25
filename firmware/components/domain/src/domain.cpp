#include "kaffee/domain.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <random>

#include "kaffee/json.hpp"
#include "kaffee/sha256.hpp"

namespace kaffee {
namespace {

Result<Event> fail(std::string code, std::string message) {
    return Result<Event>::fail(std::move(code), std::move(message));
}

bool contains(const std::vector<std::string>& v, const std::string& value) {
    return std::find(v.begin(), v.end(), value) != v.end();
}

std::string compact_id(std::string_view prefix, uint64_t value) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.*s_%016llx", static_cast<int>(prefix.size()), prefix.data(),
                  static_cast<unsigned long long>(value));
    return buf;
}

}  // namespace

// Host/test-only fallback generator. Device code must pass an IdFn backed by
// IPlatform::random_u64() to Ledger (see docs/adr/0011-id-generation.md).
//
// The engine is deliberately NOT thread_local: a 2.5 KB TLS block is
// instantiated on every task stack, including the tiny IDF IPC tasks, and
// overruns them before app_main (ready-list corruption in the scheduler). A
// process-wide engine behind a mutex keeps host tests race-free without TLS.
std::string default_id(std::string_view prefix) {
    static std::mutex rng_mutex;
    static std::mt19937_64 rng{std::random_device{}()};
    static std::atomic<uint64_t> counter{0};
    uint64_t random = 0;
    {
        std::lock_guard<std::mutex> lock(rng_mutex);
        random = rng();
    }
    return compact_id(prefix, random ^ (counter.fetch_add(1) * 0x9e3779b97f4a7c15ull));
}

Ledger::Ledger() : id_fn_([](std::string_view prefix) { return default_id(prefix); }) {}

Ledger::Ledger(IdFn id_fn) : id_fn_(std::move(id_fn)) {
    if (!id_fn_) id_fn_ = [](std::string_view prefix) { return default_id(prefix); };
}

void Ledger::set_commit_hook(CommitHook hook) {
    commit_hook_ = std::move(hook);
}
void Ledger::clear_commit_hook() {
    commit_hook_ = {};
}

std::string Ledger::event_type_name(EventType type) {
    switch (type) {
        case EventType::UserCreated:
            return "user_created";
        case EventType::UserUpdated:
            return "user_updated";
        case EventType::UserArchived:
            return "user_archived";
        case EventType::ProductCreated:
            return "product_created";
        case EventType::ProductUpdated:
            return "product_updated";
        case EventType::ProductArchived:
            return "product_archived";
        case EventType::ProductDeleted:
            return "product_deleted";
        case EventType::Purchase:
            return "purchase";
        case EventType::GuestSale:
            return "guest_sale";
        case EventType::Topup:
            return "topup";
        case EventType::Adjustment:
            return "adjustment";
        case EventType::Void:
            return "void";
        case EventType::ExpenseCreated:
            return "expense_created";
        case EventType::ExpenseUpdated:
            return "expense_updated";
        case EventType::ExpenseVoided:
            return "expense_voided";
        case EventType::CashCount:
            return "cash_count";
        case EventType::SettingsUpdated:
            return "settings_updated";
        case EventType::Audit:
            return "audit";
        case EventType::Unknown:
        default:
            return "unknown";
    }
}

EventType Ledger::event_type_from_name(std::string_view name) {
    if (name == "user_created") return EventType::UserCreated;
    if (name == "user_updated") return EventType::UserUpdated;
    if (name == "user_archived") return EventType::UserArchived;
    if (name == "product_created") return EventType::ProductCreated;
    if (name == "product_updated") return EventType::ProductUpdated;
    if (name == "product_archived") return EventType::ProductArchived;
    if (name == "product_deleted") return EventType::ProductDeleted;
    if (name == "purchase") return EventType::Purchase;
    if (name == "guest_sale") return EventType::GuestSale;
    if (name == "topup") return EventType::Topup;
    if (name == "adjustment") return EventType::Adjustment;
    if (name == "void") return EventType::Void;
    if (name == "expense_created") return EventType::ExpenseCreated;
    if (name == "expense_updated") return EventType::ExpenseUpdated;
    if (name == "expense_voided") return EventType::ExpenseVoided;
    if (name == "cash_count") return EventType::CashCount;
    if (name == "settings_updated") return EventType::SettingsUpdated;
    if (name == "audit") return EventType::Audit;
    return EventType::Unknown;
}

std::string Ledger::time_source_name(TimeSource source) {
    switch (source) {
        case TimeSource::Synced:
            return "synced";
        case TimeSource::Restored:
            return "restored";
        case TimeSource::Unset:
        default:
            return "unset";
    }
}

TimeSource Ledger::time_source_from_name(std::string_view name) {
    if (name == "synced") return TimeSource::Synced;
    if (name == "restored") return TimeSource::Restored;
    return TimeSource::Unset;
}

std::string Ledger::make_event_hash(const Event& event) {
    nlohmann::json j = event_to_json(event);
    j.erase("hash");
    return sha256_hex(j.dump());
}

Result<Event> Ledger::commit_event(Event event, std::string request_id) {
    if (!request_id.empty()) {
        auto existing = find_request(request_id);
        if (existing) return Result<Event>::ok(*existing);
    }
    event.request_id = request_id;
    if (event.id.empty()) event.id = id_fn_("evt");
    if (event.seq == 0) event.seq = next_seq_;
    event.prev_hash = head_hash_;
    event.hash = make_event_hash(event);
    auto valid = validate_event(event);
    if (!valid) return Result<Event>::fail(valid.error->code, valid.error->message);
    if (commit_hook_.fn) {
        auto persisted = commit_hook_.fn(event);
        if (!persisted) return Result<Event>::fail(persisted.error->code, persisted.error->message);
    }
    auto applied = apply(event);
    if (!applied) return Result<Event>::fail(applied.error->code, applied.error->message);
    return Result<Event>::ok(event);
}

Result<Unit> Ledger::validate_event(const Event& event) const {
    // Validate everything that can fail before mutating state.
    if ((event.type == EventType::UserCreated || event.type == EventType::UserUpdated ||
         event.type == EventType::UserArchived) &&
        (event.user_avatar_id < 0 || event.user_avatar_id > kMaxUserAvatarId))
        return Result<Unit>::fail("avatar", "invalid user avatar id");
    //
    // Event IDs must be unique across the whole ledger. Checking this here (and
    // not only in apply()) guarantees that commit_event never invokes the
    // persistence hook for an event that cannot be applied afterwards.
    if (!event.id.empty() && event_index_.count(event.id))
        return Result<Unit>::fail("duplicate", "event already exists");

    switch (event.type) {
        case EventType::UserCreated:
            if (event.user_id.empty() || event.user_name.empty())
                return Result<Unit>::fail("user", "user id and name are required");
            if (user_index_.count(event.user_id))
                return Result<Unit>::fail("user", "user already exists");
            break;
        case EventType::UserUpdated:
        case EventType::UserArchived:
            if (!user_index_.count(event.user_id))
                return Result<Unit>::fail("user", "unknown user");
            break;
        case EventType::ProductCreated:
            if (event.product_id.empty() || event.product_name.empty())
                return Result<Unit>::fail("product", "product id and name are required");
            if (product_index_.count(event.product_id))
                return Result<Unit>::fail("product", "product already exists");
            if (event.unit_amount_cents < 0)
                return Result<Unit>::fail("price", "price must not be negative");
            if (event.guest_price_cents < kNoGuestPrice || event.guest_price_cents > kMaxPriceCents)
                return Result<Unit>::fail("price", "guest price is out of range");
            break;
        case EventType::ProductUpdated:
        case EventType::ProductArchived:
        case EventType::ProductDeleted:
            if (!product_index_.count(event.product_id))
                return Result<Unit>::fail("product", "unknown product");
            if (event.guest_price_cents < kNoGuestPrice || event.guest_price_cents > kMaxPriceCents)
                return Result<Unit>::fail("price", "guest price is out of range");
            break;
        case EventType::Purchase: {
            auto ui = user_index_.find(event.user_id);
            auto pi = product_index_.find(event.product_id);
            if (ui == user_index_.end()) return Result<Unit>::fail("user", "unknown user");
            if (pi == product_index_.end()) return Result<Unit>::fail("product", "unknown product");
            if (!users_[ui->second].active) return Result<Unit>::fail("user", "user is archived");
            if (!products_[pi->second].active)
                return Result<Unit>::fail("product", "product is archived");
            if (event.amount_cents != -event.unit_amount_cents)
                return Result<Unit>::fail("amount", "purchase amount mismatch");
            break;
        }
        case EventType::GuestSale: {
            auto pi = product_index_.find(event.product_id);
            if (pi == product_index_.end()) return Result<Unit>::fail("product", "unknown product");
            if (!products_[pi->second].active)
                return Result<Unit>::fail("product", "product is archived");
            if (event.amount_cents != event.unit_amount_cents)
                return Result<Unit>::fail("amount", "guest sale amount mismatch");
            break;
        }
        case EventType::Topup:
        case EventType::Adjustment:
            if (!user_index_.count(event.user_id))
                return Result<Unit>::fail("user", "unknown user");
            if (event.type == EventType::Topup && event.amount_cents <= 0)
                return Result<Unit>::fail("amount", "top-up must be positive");
            if (event.type == EventType::Adjustment && event.amount_cents == 0)
                return Result<Unit>::fail("amount", "adjustment must not be zero");
            break;
        case EventType::Void: {
            auto it = event_index_.find(event.reverses);
            if (it == event_index_.end())
                return Result<Unit>::fail("void", "original event not found");
            const Event& original = events_[it->second];
            if (original.type == EventType::Void)
                return Result<Unit>::fail("void", "cannot void a void");
            if (original.type == EventType::ExpenseCreated ||
                original.type == EventType::ExpenseUpdated ||
                original.type == EventType::ExpenseVoided)
                return Result<Unit>::fail("void", "use the expense void endpoint");
            if (original.type == EventType::SettingsUpdated || original.type == EventType::Audit ||
                original.type == EventType::CashCount)
                return Result<Unit>::fail("void", "event type cannot be voided");
            if (contains(voided_, original.id))
                return Result<Unit>::fail("void", "event is already voided");
            if (event.amount_cents != -original.amount_cents)
                return Result<Unit>::fail("amount", "void amount mismatch");
            break;
        }
        case EventType::ExpenseCreated:
            if (event.expense_id.empty() || event.amount_cents > 0)
                return Result<Unit>::fail("expense", "invalid expense");
            if (expense_index_.count(event.expense_id))
                return Result<Unit>::fail("expense", "expense already exists");
            break;
        case EventType::ExpenseUpdated: {
            auto it = expense_index_.find(event.expense_id);
            if (it == expense_index_.end()) return Result<Unit>::fail("expense", "unknown expense");
            if (expenses_[it->second].voided)
                return Result<Unit>::fail("expense", "expense is voided");
            break;
        }
        case EventType::ExpenseVoided: {
            auto it = expense_index_.find(event.expense_id);
            if (it == expense_index_.end()) return Result<Unit>::fail("expense", "unknown expense");
            if (expenses_[it->second].voided)
                return Result<Unit>::fail("expense", "expense is already voided");
            break;
        }
        case EventType::CashCount:
        case EventType::SettingsUpdated:
        case EventType::Audit:
            break;
        case EventType::Unknown:
        default:
            return Result<Unit>::fail("type", "unknown event type");
    }

    return Result<Unit>::ok(Unit{});
}

Result<Unit> Ledger::apply(const Event& event) {
    if (event.seq != next_seq_) {
        return Result<Unit>::fail("seq", "unexpected event sequence");
    }
    if (event.id.empty()) return Result<Unit>::fail("id", "event id is empty");
    if (event_index_.count(event.id))
        return Result<Unit>::fail("duplicate", "event already applied");
    if (event.prev_hash != head_hash_) {
        return Result<Unit>::fail("chain", "hash chain is broken");
    }
    if (event.hash != make_event_hash(event)) {
        return Result<Unit>::fail("hash", "event hash mismatch");
    }

    auto valid = validate_event(event);
    if (!valid) return valid;

    switch (event.type) {
        case EventType::UserCreated: {
            User u;
            u.id = event.user_id;
            u.name = event.user_name;
            u.color = event.user_color;
            u.avatar_id = event.user_avatar_id;
            u.active = event.user_active;
            u.created_at = event.ts;
            user_index_[u.id] = users_.size();
            users_.push_back(std::move(u));
            balances_[event.user_id] = 0;
            break;
        }
        case EventType::UserUpdated:
        case EventType::UserArchived: {
            User& u = users_[user_index_.at(event.user_id)];
            u.name = event.user_name;
            u.color = event.user_color;
            u.avatar_id = event.user_avatar_id;
            u.active = event.user_active;
            if (!u.active)
                u.archived_at = event.ts;
            else
                u.archived_at = 0;
            break;
        }
        case EventType::ProductCreated: {
            Product p;
            p.id = event.product_id;
            p.name = event.product_name;
            p.category = event.product_category;
            p.icon = event.product_icon;
            p.color = event.product_color;
            p.price_cents = event.unit_amount_cents;
            p.guest_price_cents = event.guest_price_cents;
            p.sort = event.product_sort;
            p.active = event.product_active;
            p.created_at = event.ts;
            p.updated_at = event.ts;
            product_index_[p.id] = products_.size();
            products_.push_back(std::move(p));
            break;
        }
        case EventType::ProductUpdated:
        case EventType::ProductArchived: {
            Product& p = products_[product_index_.at(event.product_id)];
            p.name = event.product_name;
            p.category = event.product_category;
            p.icon = event.product_icon;
            p.color = event.product_color;
            p.price_cents = event.unit_amount_cents;
            p.guest_price_cents = event.guest_price_cents;
            p.sort = event.product_sort;
            p.active = event.product_active;
            p.updated_at = event.ts;
            break;
        }
        case EventType::ProductDeleted: {
            auto it = product_index_.find(event.product_id);
            if (it != product_index_.end()) {
                products_.erase(products_.begin() + static_cast<std::ptrdiff_t>(it->second));
                product_index_.clear();
                for (size_t i = 0; i < products_.size(); ++i) product_index_[products_[i].id] = i;
            }
            break;
        }
        case EventType::Purchase: {
            balances_[event.user_id] += event.amount_cents;
            member_sales_cents_ += -event.amount_cents;
            member_items_ += 1;
            break;
        }
        case EventType::GuestSale: {
            cash_cents_ += event.amount_cents;
            guest_sales_cents_ += event.amount_cents;
            guest_items_ += 1;
            break;
        }
        case EventType::Topup: {
            balances_[event.user_id] += event.amount_cents;
            cash_cents_ += event.amount_cents;
            topups_cents_ += event.amount_cents;
            break;
        }
        case EventType::Adjustment: {
            balances_[event.user_id] += event.amount_cents;
            break;
        }
        case EventType::Void: {
            const Event& original = events_[event_index_.at(event.reverses)];
            switch (original.type) {
                case EventType::Purchase:
                    balances_[original.user_id] -= original.amount_cents;
                    member_sales_cents_ -= -original.amount_cents;
                    member_items_ -= 1;
                    break;
                case EventType::GuestSale:
                    cash_cents_ -= original.amount_cents;
                    guest_sales_cents_ -= original.amount_cents;
                    guest_items_ -= 1;
                    break;
                case EventType::Topup:
                    balances_[original.user_id] -= original.amount_cents;
                    cash_cents_ -= original.amount_cents;
                    topups_cents_ -= original.amount_cents;
                    break;
                case EventType::Adjustment:
                    balances_[original.user_id] -= original.amount_cents;
                    break;
                default:
                    break;
            }
            voided_.push_back(original.id);
            break;
        }
        case EventType::ExpenseCreated: {
            Expense e;
            e.id = event.expense_id;
            e.ts = event.ts;
            e.category = event.expense_category;
            e.description = event.expense_description;
            e.vendor = event.expense_vendor;
            e.amount_cents = -event.amount_cents;
            e.note = event.note;
            expense_index_[e.id] = expenses_.size();
            expenses_.push_back(std::move(e));
            cash_cents_ += event.amount_cents;
            expenses_cents_ += -event.amount_cents;
            break;
        }
        case EventType::ExpenseUpdated: {
            Expense& e = expenses_[expense_index_.at(event.expense_id)];
            Cents new_amount = event.unit_amount_cents;
            Cents delta = new_amount - e.amount_cents;
            e.category = event.expense_category;
            e.description = event.expense_description;
            e.vendor = event.expense_vendor;
            e.amount_cents = new_amount;
            e.note = event.note;
            cash_cents_ -= delta;
            expenses_cents_ += delta;
            break;
        }
        case EventType::ExpenseVoided: {
            Expense& e = expenses_[expense_index_.at(event.expense_id)];
            cash_cents_ += e.amount_cents;
            expenses_cents_ -= e.amount_cents;
            e.voided = true;
            voided_.push_back(e.id);
            break;
        }
        case EventType::CashCount: {
            CashCount c;
            c.id = event.id;
            c.ts = event.ts;
            c.counted_cents = event.counted_cents;
            c.expected_cents = event.expected_cents;
            c.note = event.note;
            cash_counts_.push_back(std::move(c));
            break;
        }
        case EventType::SettingsUpdated:
        case EventType::Audit:
        case EventType::Unknown:
            break;
    }

    event_index_[event.id] = events_.size();
    if (!event.request_id.empty()) request_index_[event.request_id] = events_.size();
    events_.push_back(event);
    next_seq_ = event.seq + 1;
    head_hash_ = event.hash;
    return Result<Unit>::ok(Unit{});
}

void Ledger::reset() {
    users_.clear();
    products_.clear();
    events_.clear();
    expenses_.clear();
    cash_counts_.clear();
    balances_.clear();
    request_index_.clear();
    event_index_.clear();
    expense_index_.clear();
    user_index_.clear();
    product_index_.clear();
    voided_.clear();
    next_seq_ = 1;
    head_hash_.clear();
    cash_cents_ = 0;
    member_sales_cents_ = 0;
    guest_sales_cents_ = 0;
    topups_cents_ = 0;
    expenses_cents_ = 0;
    member_items_ = 0;
    guest_items_ = 0;
}

Result<Event> Ledger::create_user(std::string name, std::string color, std::string request_id,
                                  Timestamp ts, TimeSource source, int avatar_id) {
    if (name.empty()) return fail("name", "name is required");
    Event e;
    e.type = EventType::UserCreated;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.user_id = id_fn_("usr");
    e.user_name = std::move(name);
    e.user_color = std::move(color);
    e.user_avatar_id = avatar_id;
    e.user_active = true;
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::update_user(std::string user_id, std::string name, std::string color,
                                  bool active, std::string request_id, Timestamp ts,
                                  TimeSource source, int avatar_id) {
    auto it = user_index_.find(user_id);
    if (it == user_index_.end()) return fail("user", "unknown user");
    if (name.empty()) return fail("name", "name is required");
    Event e;
    e.type = active ? EventType::UserUpdated : EventType::UserArchived;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.user_id = std::move(user_id);
    e.user_name = std::move(name);
    e.user_color = std::move(color);
    e.user_avatar_id = avatar_id < 0 ? users_[it->second].avatar_id : avatar_id;
    e.user_active = active;
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::archive_user(std::string user_id, std::string request_id, Timestamp ts,
                                   TimeSource source) {
    auto it = user_index_.find(user_id);
    if (it == user_index_.end()) return fail("user", "unknown user");
    const User& u = users_[it->second];
    return update_user(u.id, u.name, u.color, false, std::move(request_id), ts, source);
}

Result<Event> Ledger::create_product(std::string name, std::string category, std::string icon,
                                     std::string color, Cents price_cents, int sort,
                                     std::string request_id, Timestamp ts, TimeSource source,
                                     Cents guest_price_cents) {
    if (name.empty()) return fail("name", "name is required");
    if (price_cents < 0) return fail("price", "price must not be negative");
    if (guest_price_cents < kNoGuestPrice || guest_price_cents > kMaxPriceCents)
        return fail("price", "guest price is out of range");
    Event e;
    e.type = EventType::ProductCreated;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.product_id = id_fn_("prd");
    e.product_name = std::move(name);
    e.product_category = std::move(category);
    e.product_icon = std::move(icon);
    e.product_color = std::move(color);
    e.unit_amount_cents = price_cents;
    e.guest_price_cents = guest_price_cents;
    e.product_sort = sort;
    e.product_active = true;
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::update_product(std::string product_id, std::string name, std::string category,
                                     std::string icon, std::string color, Cents price_cents,
                                     int sort, bool active, std::string request_id, Timestamp ts,
                                     TimeSource source, Cents guest_price_cents) {
    if (!product_index_.count(product_id)) return fail("product", "unknown product");
    if (name.empty()) return fail("name", "name is required");
    if (price_cents < 0) return fail("price", "price must not be negative");
    if (guest_price_cents < kNoGuestPrice || guest_price_cents > kMaxPriceCents)
        return fail("price", "guest price is out of range");
    Event e;
    e.type = active ? EventType::ProductUpdated : EventType::ProductArchived;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.product_id = std::move(product_id);
    e.product_name = std::move(name);
    e.product_category = std::move(category);
    e.product_icon = std::move(icon);
    e.product_color = std::move(color);
    e.unit_amount_cents = price_cents;
    e.guest_price_cents = guest_price_cents;
    e.product_sort = sort;
    e.product_active = active;
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::archive_product(std::string product_id, std::string request_id, Timestamp ts,
                                      TimeSource source) {
    auto it = product_index_.find(product_id);
    if (it == product_index_.end()) return fail("product", "unknown product");
    const Product& p = products_[it->second];
    return update_product(p.id, p.name, p.category, p.icon, p.color, p.price_cents, p.sort, false,
                          std::move(request_id), ts, source, p.guest_price_cents);
}

Result<Event> Ledger::delete_product(std::string product_id, std::string request_id, Timestamp ts,
                                     TimeSource source) {
    auto it = product_index_.find(product_id);
    if (it == product_index_.end()) return fail("product", "unknown product");
    const Product& p = products_[it->second];
    Event e;
    e.type = EventType::ProductDeleted;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.product_id = p.id;
    e.product_name = p.name;
    e.product_category = p.category;
    e.product_icon = p.icon;
    e.product_color = p.color;
    e.unit_amount_cents = p.price_cents;
    e.guest_price_cents = p.guest_price_cents;
    e.product_sort = p.sort;
    e.product_active = false;
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::purchase(std::string user_id, std::string product_id, std::string request_id,
                               Timestamp ts, TimeSource source, std::string note) {
    auto ui = user_index_.find(user_id);
    auto pi = product_index_.find(product_id);
    if (ui == user_index_.end()) return fail("user", "unknown user");
    if (pi == product_index_.end()) return fail("product", "unknown product");
    const User& u = users_[ui->second];
    const Product& p = products_[pi->second];
    if (!u.active) return fail("user", "user is archived");
    if (!p.active) return fail("product", "product is archived");
    Event e;
    e.type = EventType::Purchase;
    e.ts = ts;
    e.time_source = source;
    e.actor = "ui";
    e.user_id = u.id;
    e.user_name = u.name;
    e.user_color = u.color;
    e.user_active = u.active;
    e.product_id = p.id;
    e.product_name = p.name;
    e.product_category = p.category;
    e.product_icon = p.icon;
    e.product_color = p.color;
    e.unit_amount_cents = p.price_cents;
    e.product_sort = p.sort;
    e.product_active = p.active;
    e.amount_cents = -p.price_cents;
    e.payment = "balance";
    e.note = std::move(note);
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::guest_sale(std::string product_id, std::string request_id, Timestamp ts,
                                 TimeSource source, std::string note) {
    auto pi = product_index_.find(product_id);
    if (pi == product_index_.end()) return fail("product", "unknown product");
    const Product& p = products_[pi->second];
    if (!p.active) return fail("product", "product is archived");
    // A guest pays the configured guest price; without one the member price
    // applies. The charged amount is snapshotted in the event below so later
    // price edits cannot change historical guest revenue.
    const Cents charged = p.guest_price_cents >= 0 ? p.guest_price_cents : p.price_cents;
    Event e;
    e.type = EventType::GuestSale;
    e.ts = ts;
    e.time_source = source;
    e.actor = "ui";
    e.product_id = p.id;
    e.product_name = p.name;
    e.product_category = p.category;
    e.product_icon = p.icon;
    e.product_color = p.color;
    e.unit_amount_cents = charged;
    e.guest_price_cents = p.guest_price_cents;
    e.product_sort = p.sort;
    e.product_active = p.active;
    e.amount_cents = charged;
    e.payment = "cash";
    e.note = std::move(note);
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::topup(std::string user_id, Cents amount_cents, std::string note,
                            std::string request_id, Timestamp ts, TimeSource source) {
    auto ui = user_index_.find(user_id);
    if (ui == user_index_.end()) return fail("user", "unknown user");
    if (amount_cents <= 0) return fail("amount", "top-up must be positive");
    const User& u = users_[ui->second];
    Event e;
    e.type = EventType::Topup;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.user_id = u.id;
    e.user_name = u.name;
    e.user_color = u.color;
    e.user_active = u.active;
    e.amount_cents = amount_cents;
    e.payment = "cash";
    e.note = std::move(note);
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::adjust(std::string user_id, Cents amount_cents, std::string note,
                             std::string request_id, Timestamp ts, TimeSource source) {
    auto ui = user_index_.find(user_id);
    if (ui == user_index_.end()) return fail("user", "unknown user");
    if (amount_cents == 0) return fail("amount", "adjustment must not be zero");
    if (note.empty()) return fail("note", "note is required");
    const User& u = users_[ui->second];
    Event e;
    e.type = EventType::Adjustment;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.user_id = u.id;
    e.user_name = u.name;
    e.user_color = u.color;
    e.user_active = u.active;
    e.amount_cents = amount_cents;
    e.payment = "balance";
    e.note = std::move(note);
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::void_event(std::string event_id, std::string reason, std::string request_id,
                                 Timestamp ts, TimeSource source, std::string actor) {
    auto it = event_index_.find(event_id);
    if (it == event_index_.end()) return fail("event", "unknown event");
    const Event& orig = events_[it->second];
    if (orig.type == EventType::Void || orig.type == EventType::ExpenseCreated ||
        orig.type == EventType::ExpenseUpdated || orig.type == EventType::ExpenseVoided ||
        orig.type == EventType::SettingsUpdated || orig.type == EventType::Audit ||
        orig.type == EventType::CashCount) {
        return fail("void", "event type cannot be voided");
    }
    if (contains(voided_, orig.id)) return fail("void", "event is already voided");
    if (reason.empty()) return fail("reason", "reason is required");
    Event e;
    e.type = EventType::Void;
    e.ts = ts;
    e.time_source = source;
    e.actor = std::move(actor);
    e.user_id = orig.user_id;
    e.user_name = orig.user_name;
    e.user_color = orig.user_color;
    e.user_active = orig.user_active;
    e.product_id = orig.product_id;
    e.product_name = orig.product_name;
    e.product_category = orig.product_category;
    e.product_icon = orig.product_icon;
    e.product_color = orig.product_color;
    e.product_sort = orig.product_sort;
    e.product_active = orig.product_active;
    e.unit_amount_cents = orig.unit_amount_cents;
    e.guest_price_cents = orig.guest_price_cents;
    e.amount_cents = -orig.amount_cents;
    e.payment = orig.payment;
    e.note = std::move(reason);
    e.reverses = orig.id;
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::add_expense(std::string category, std::string description, std::string vendor,
                                  Cents amount_cents, std::string note, std::string request_id,
                                  Timestamp ts, TimeSource source) {
    if (amount_cents <= 0) return fail("amount", "expense must be positive");
    if (description.empty()) return fail("description", "description is required");
    Event e;
    e.type = EventType::ExpenseCreated;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.id = id_fn_("exp");
    e.expense_id = e.id;
    e.expense_category = std::move(category);
    e.expense_description = std::move(description);
    e.expense_vendor = std::move(vendor);
    e.amount_cents = -amount_cents;
    e.unit_amount_cents = amount_cents;
    e.note = std::move(note);
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::update_expense(std::string expense_id, std::string category,
                                     std::string description, std::string vendor,
                                     Cents amount_cents, std::string note, std::string request_id,
                                     Timestamp ts, TimeSource source) {
    auto it = expense_index_.find(expense_id);
    if (it == expense_index_.end()) return fail("expense", "unknown expense");
    const Expense& old = expenses_[it->second];
    if (old.voided) return fail("expense", "expense is voided");
    if (amount_cents <= 0) return fail("amount", "expense must be positive");
    if (description.empty()) return fail("description", "description is required");
    Cents delta = amount_cents - old.amount_cents;
    Event e;
    e.type = EventType::ExpenseUpdated;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.expense_id = old.id;
    e.expense_category = std::move(category);
    e.expense_description = std::move(description);
    e.expense_vendor = std::move(vendor);
    e.unit_amount_cents = amount_cents;
    e.amount_cents = -delta;
    e.note = std::move(note);
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::void_expense(std::string expense_id, std::string reason,
                                   std::string request_id, Timestamp ts, TimeSource source) {
    auto it = expense_index_.find(expense_id);
    if (it == expense_index_.end()) return fail("expense", "unknown expense");
    const Expense& exp = expenses_[it->second];
    if (exp.voided) return fail("expense", "expense is already voided");
    if (reason.empty()) return fail("reason", "reason is required");
    Event e;
    e.type = EventType::ExpenseVoided;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.expense_id = exp.id;
    e.expense_category = exp.category;
    e.expense_description = exp.description;
    e.expense_vendor = exp.vendor;
    e.unit_amount_cents = exp.amount_cents;
    e.amount_cents = exp.amount_cents;
    e.note = std::move(reason);
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::record_cash_count(Cents counted_cents, std::string note,
                                        std::string request_id, Timestamp ts, TimeSource source) {
    if (counted_cents < 0) return fail("amount", "cash count must not be negative");
    Event e;
    e.type = EventType::CashCount;
    e.ts = ts;
    e.time_source = source;
    e.actor = "admin";
    e.id = id_fn_("cash");
    e.counted_cents = counted_cents;
    e.expected_cents = cash_cents_;
    e.note = std::move(note);
    return commit_event(std::move(e), std::move(request_id));
}

Result<Event> Ledger::audit(std::string kind, std::string meta_json, std::string actor,
                            std::string request_id, Timestamp ts, TimeSource source) {
    Event e;
    e.type = EventType::Audit;
    e.ts = ts;
    e.time_source = source;
    e.actor = std::move(actor);
    e.note = std::move(kind);
    e.meta_json = std::move(meta_json);
    return commit_event(std::move(e), std::move(request_id));
}

std::optional<User> Ledger::find_user(const std::string& id) const {
    auto it = user_index_.find(id);
    if (it == user_index_.end()) return std::nullopt;
    return users_[it->second];
}

std::optional<Product> Ledger::find_product(const std::string& id) const {
    auto it = product_index_.find(id);
    if (it == product_index_.end()) return std::nullopt;
    return products_[it->second];
}

std::optional<Event> Ledger::find_event(const std::string& id) const {
    auto it = event_index_.find(id);
    if (it == event_index_.end()) return std::nullopt;
    return events_[it->second];
}

std::optional<Event> Ledger::find_request(const std::string& request_id) const {
    auto it = request_index_.find(request_id);
    if (it == request_index_.end()) return std::nullopt;
    return events_[it->second];
}

const std::vector<Expense>& Ledger::active_expenses() const {
    return expenses_;
}

bool Ledger::is_voided(const std::string& event_id) const {
    return contains(voided_, event_id);
}

Cents Ledger::balance(const std::string& user_id) const {
    auto it = balances_.find(user_id);
    return it == balances_.end() ? 0 : it->second;
}

Summary Ledger::summary() const {
    Summary s;
    for (const auto& [id, balance] : balances_) {
        (void)id;
        if (balance > 0)
            s.prepaid += balance;
        else if (balance < 0)
            s.debts += -balance;
    }
    s.net_liabilities = s.prepaid - s.debts;
    s.cash_expected = cash_cents_;
    s.equity = s.cash_expected - s.net_liabilities;
    s.member_sales = member_sales_cents_;
    s.guest_sales = guest_sales_cents_;
    s.topups = topups_cents_;
    s.expenses = expenses_cents_;
    s.member_items = member_items_;
    s.guest_items = guest_items_;
    return s;
}

Leaderboard Ledger::leaderboard(Timestamp from, Timestamp to) const {
    Leaderboard out;
    out.from = from;
    out.to = to;
    std::map<std::string, LeaderboardEntry> entries;
    for (const User& u : users_) {
        entries[u.id] = LeaderboardEntry{u.id, u.name, u.color, 0};
    }
    for (const Event& e : events_) {
        if (e.ts < from || e.ts >= to) continue;
        if (e.type == EventType::Purchase) {
            auto& entry = entries[e.user_id];
            entry.user_id = e.user_id;
            entry.name = e.user_name;
            entry.color = e.user_color;
            entry.count += 1;
        } else if (e.type == EventType::GuestSale) {
            out.guest_count += 1;
        } else if (e.type == EventType::Void) {
            auto original = find_event(e.reverses);
            if (!original) continue;
            // Count the correction in the period of the original booking, not
            // the period of the void. A void in a later month must not make
            // that month's leaderboard negative.
            if (original->ts < from || original->ts >= to) continue;
            if (original->type == EventType::Purchase) {
                auto& entry = entries[original->user_id];
                entry.user_id = original->user_id;
                entry.name = original->user_name;
                entry.color = original->user_color;
                entry.count -= 1;
            } else if (original->type == EventType::GuestSale) {
                out.guest_count -= 1;
            }
        }
    }
    for (auto& [id, entry] : entries) {
        (void)id;
        out.entries.push_back(entry);
    }
    std::sort(out.entries.begin(), out.entries.end(),
              [](const LeaderboardEntry& a, const LeaderboardEntry& b) {
                  if (a.count != b.count) return a.count > b.count;
                  return a.name < b.name;
              });
    return out;
}

std::vector<std::pair<std::string, Cents>> Ledger::balances() const {
    std::vector<std::pair<std::string, Cents>> out;
    out.reserve(balances_.size());
    for (const auto& entry : balances_) out.push_back(entry);
    return out;
}

}  // namespace kaffee
