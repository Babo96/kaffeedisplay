#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kaffee/fwd.hpp"

namespace kaffee {

using Timestamp = int64_t;  // Unix seconds, UTC

inline constexpr int kMaxUserAvatarId = 30;  // 0 = initials only; 1..30 = fixed presets

// Upper bound shared by the domain and the REST layer for a single unit price.
inline constexpr Cents kMaxPriceCents = 100000;  // 1000.00 EUR

// Sentinel for "no separate guest price": the guest pays the member price.
// Prices themselves are never negative (0 is a valid, free price), so this
// mirrors the `avatar_id = -1` convention of the user API. It is omitted from
// the event JSON when unset, which keeps the hashes of older journals valid.
inline constexpr Cents kNoGuestPrice = -1;

enum class TimeSource { Synced, Restored, Unset };
enum class EventType {
    Unknown,
    UserCreated,
    UserUpdated,
    UserArchived,
    ProductCreated,
    ProductUpdated,
    ProductArchived,
    ProductDeleted,
    Purchase,
    GuestSale,
    Topup,
    Adjustment,
    Void,
    ExpenseCreated,
    ExpenseUpdated,
    ExpenseVoided,
    CashCount,
    SettingsUpdated,
    Audit,
};

struct Error {
    std::string code;
    std::string message;
};

template <typename T>
struct Result {
    std::optional<T> value;
    std::optional<Error> error;

    static Result ok(T v) {
        return Result{std::move(v), std::nullopt};
    }
    static Result fail(std::string code, std::string message) {
        return Result{std::nullopt, Error{std::move(code), std::move(message)}};
    }
    explicit operator bool() const {
        return value.has_value();
    }
};

struct Unit {};

struct User {
    std::string id;
    std::string name;
    std::string color;  // UI accent colour, e.g. "#D4A373"
    bool active = true;
    Timestamp created_at = 0;
    Timestamp archived_at = 0;
    int avatar_id = 0;  // 0 = initials only
};

struct Product {
    std::string id;
    std::string name;
    std::string category;  // Kaffee, Tee, Snacks, Sonstiges
    std::string icon;      // icon key understood by the UI
    std::string color;     // UI accent colour
    Cents price_cents = 0;
    // Optional separate price for guest sales. kNoGuestPrice (-1) means the
    // guest pays `price_cents`; any value >= 0 is charged to guests instead.
    Cents guest_price_cents = kNoGuestPrice;
    int sort = 0;
    bool active = true;
    Timestamp created_at = 0;
    Timestamp updated_at = 0;
};

struct Expense {
    std::string id;
    Timestamp ts = 0;
    std::string category;
    std::string description;
    std::string vendor;
    Cents amount_cents = 0;  // positive amount
    std::string note;
    bool voided = false;
};

struct CashCount {
    std::string id;
    Timestamp ts = 0;
    Cents counted_cents = 0;
    Cents expected_cents = 0;
    std::string note;
};

struct Event {
    uint64_t seq = 0;
    std::string id;
    Timestamp ts = 0;
    TimeSource time_source = TimeSource::Unset;
    EventType type = EventType::Unknown;

    // User snapshot (empty when not applicable).
    std::string user_id;
    std::string user_name;
    std::string user_color;
    int user_avatar_id = 0;
    bool user_active = true;

    // Product snapshot (empty when not applicable).
    std::string product_id;
    std::string product_name;
    std::string product_category;
    std::string product_icon;
    std::string product_color;
    int product_sort = 0;
    bool product_active = true;

    // Unit price for purchases / product events. `amount_cents` is the full
    // financial delta and is authoritative for balances.
    Cents unit_amount_cents = 0;

    // Product's optional guest price snapshot for product events and guest
    // sales. kNoGuestPrice when the product inherited the member price; the
    // amount actually charged is always `unit_amount_cents`/`amount_cents`.
    Cents guest_price_cents = kNoGuestPrice;

    // Financial delta: purchase/expense negative, topup/guest sale positive,
    // adjustment either, void the exact opposite of the original.
    Cents amount_cents = 0;

    // Expense snapshot (empty when not applicable).
    std::string expense_id;
    std::string expense_category;
    std::string expense_description;
    std::string expense_vendor;

    // Cash count.
    Cents counted_cents = 0;
    Cents expected_cents = 0;

    std::string payment;  // "balance" or "cash"
    std::string note;
    std::string actor;  // "ui", "admin", "system"
    std::string request_id;
    std::string reverses;   // event id for Void
    std::string meta_json;  // audit/settings payload, never used for money

    std::string prev_hash;
    std::string hash;
};

struct Summary {
    Cents cash_expected = 0;
    Cents prepaid = 0;
    Cents debts = 0;
    Cents net_liabilities = 0;
    Cents equity = 0;
    Cents member_sales = 0;
    Cents guest_sales = 0;
    Cents topups = 0;
    Cents expenses = 0;
    int member_items = 0;
    int guest_items = 0;
};

struct LeaderboardEntry {
    std::string user_id;
    std::string name;
    std::string color;
    int count = 0;
};

struct Leaderboard {
    Timestamp from = 0;
    Timestamp to = 0;
    std::vector<LeaderboardEntry> entries;
    int guest_count = 0;
};

struct CommitHook {
    std::function<Result<Unit>(const Event&)> fn;
};

class Ledger {
  public:
    using IdFn = std::function<std::string(std::string_view prefix)>;

    // IDs are produced by the injected `IdFn` as "<prefix>_<16 lowercase hex
    // chars>". The generator is injected so the domain layer stays free of
    // platform dependencies.
    //
    // The default constructor's generator (`default_id`) is HOST/TEST ONLY: it
    // is backed by std::random_device/mt19937_64, not by the ESP32 hardware
    // RNG, and must not be used on a device. KaffeeService must construct its
    // ledger with an `IdFn` backed by `IPlatform::random_u64()`, e.g.
    //
    //   Ledger([&platform](std::string_view prefix) {
    //       char buf[sizeof("exp_0123456789abcdef")];
    //       std::snprintf(buf, sizeof(buf), "%.*s_%016llx",
    //                     static_cast<int>(prefix.size()), prefix.data(),
    //                     static_cast<unsigned long long>(platform.random_u64()));
    //       return std::string(buf);
    //   });
    //
    // That keeps generated IDs reboot-safe and collision-free against replayed
    // history. See docs/adr/0011-id-generation.md.
    Ledger();
    explicit Ledger(IdFn id_fn);

    void set_commit_hook(CommitHook hook);
    void clear_commit_hook();

    // Mutations persist through the commit hook before mutating memory.
    Result<Event> create_user(std::string name, std::string color, std::string request_id,
                              Timestamp ts, TimeSource source, int avatar_id = 0);
    Result<Event> update_user(std::string user_id, std::string name, std::string color, bool active,
                              std::string request_id, Timestamp ts, TimeSource source,
                              int avatar_id = -1);
    Result<Event> archive_user(std::string user_id, std::string request_id, Timestamp ts,
                               TimeSource source);

    Result<Event> create_product(std::string name, std::string category, std::string icon,
                                 std::string color, Cents price_cents, int sort,
                                 std::string request_id, Timestamp ts, TimeSource source,
                                 Cents guest_price_cents = kNoGuestPrice);
    Result<Event> update_product(std::string product_id, std::string name, std::string category,
                                 std::string icon, std::string color, Cents price_cents, int sort,
                                 bool active, std::string request_id, Timestamp ts,
                                 TimeSource source, Cents guest_price_cents = kNoGuestPrice);
    Result<Event> archive_product(std::string product_id, std::string request_id, Timestamp ts,
                                  TimeSource source);
    Result<Event> delete_product(std::string product_id, std::string request_id, Timestamp ts,
                                 TimeSource source);

    Result<Event> purchase(std::string user_id, std::string product_id, std::string request_id,
                           Timestamp ts, TimeSource source, std::string note = "");
    Result<Event> guest_sale(std::string product_id, std::string request_id, Timestamp ts,
                             TimeSource source, std::string note = "");
    Result<Event> topup(std::string user_id, Cents amount_cents, std::string note,
                        std::string request_id, Timestamp ts, TimeSource source);
    Result<Event> adjust(std::string user_id, Cents amount_cents, std::string note,
                         std::string request_id, Timestamp ts, TimeSource source);
    Result<Event> void_event(std::string event_id, std::string reason, std::string request_id,
                             Timestamp ts, TimeSource source, std::string actor = "admin");

    Result<Event> add_expense(std::string category, std::string description, std::string vendor,
                              Cents amount_cents, std::string note, std::string request_id,
                              Timestamp ts, TimeSource source);
    Result<Event> update_expense(std::string expense_id, std::string category,
                                 std::string description, std::string vendor, Cents amount_cents,
                                 std::string note, std::string request_id, Timestamp ts,
                                 TimeSource source);
    Result<Event> void_expense(std::string expense_id, std::string reason, std::string request_id,
                               Timestamp ts, TimeSource source);
    Result<Event> record_cash_count(Cents counted_cents, std::string note, std::string request_id,
                                    Timestamp ts, TimeSource source);

    Result<Event> audit(std::string kind, std::string meta_json, std::string actor,
                        std::string request_id, Timestamp ts, TimeSource source);

    // Replay an existing event. Used by storage at boot.
    Result<Unit> apply(const Event& event);
    void reset();

    const std::vector<User>& users() const {
        return users_;
    }
    const std::vector<Product>& products() const {
        return products_;
    }
    const std::vector<Event>& events() const {
        return events_;
    }
    const std::vector<Expense>& expenses() const {
        return expenses_;
    }
    const std::vector<CashCount>& cash_counts() const {
        return cash_counts_;
    }

    std::optional<User> find_user(const std::string& id) const;
    std::optional<Product> find_product(const std::string& id) const;
    std::optional<Event> find_event(const std::string& id) const;
    std::optional<Event> find_request(const std::string& request_id) const;
    const std::vector<Expense>& active_expenses() const;
    bool is_voided(const std::string& event_id) const;

    Cents balance(const std::string& user_id) const;
    Summary summary() const;
    Leaderboard leaderboard(Timestamp from, Timestamp to) const;
    std::vector<std::pair<std::string, Cents>> balances() const;

    uint64_t next_seq() const {
        return next_seq_;
    }
    std::string head_hash() const {
        return head_hash_;
    }

    static std::string make_event_hash(const Event& event);
    static std::string event_type_name(EventType type);
    static EventType event_type_from_name(std::string_view name);
    static std::string time_source_name(TimeSource source);
    static TimeSource time_source_from_name(std::string_view name);

  private:
    IdFn id_fn_;
    CommitHook commit_hook_;

    std::vector<User> users_;
    std::vector<Product> products_;
    std::vector<Event> events_;
    std::vector<Expense> expenses_;
    std::vector<CashCount> cash_counts_;
    std::map<std::string, Cents> balances_;
    std::map<std::string, size_t> request_index_;
    std::map<std::string, size_t> event_index_;
    std::map<std::string, size_t> expense_index_;
    std::map<std::string, size_t> user_index_;
    std::map<std::string, size_t> product_index_;
    std::vector<std::string> voided_;

    uint64_t next_seq_ = 1;
    std::string head_hash_;
    Cents cash_cents_ = 0;
    Cents member_sales_cents_ = 0;
    Cents guest_sales_cents_ = 0;
    Cents topups_cents_ = 0;
    Cents expenses_cents_ = 0;
    int member_items_ = 0;
    int guest_items_ = 0;

    // Builds, persists and applies a new event.
    Result<Event> commit_event(Event event, std::string request_id);
    Result<Unit> validate_event(const Event& event) const;
};

// Host/test-only fallback ID generator. Device code (KaffeeService) must inject
// an IdFn backed by IPlatform::random_u64() instead; see
// docs/adr/0011-id-generation.md.
std::string default_id(std::string_view prefix);

}  // namespace kaffee
