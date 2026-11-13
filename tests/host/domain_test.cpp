#include <doctest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "kaffee/domain.hpp"
#include "kaffee/json.hpp"
#include "kaffee/sha256.hpp"

using namespace kaffee;

namespace {

Timestamp f_now() {
    return 1'700'000'000;
}

struct Fixture {
    Ledger ledger;
    std::vector<Event> persisted;
    Timestamp now = 1'700'000'000;

    Fixture() {
        auto counter = std::make_shared<int>(0);
        ledger = Ledger([counter](std::string_view prefix) {
            return std::string(prefix) + "_" + std::to_string(++*counter);
        });
        ledger.set_commit_hook(CommitHook{[this](const Event& e) -> Result<Unit> {
            persisted.push_back(e);
            return Result<Unit>::ok(Unit{});
        }});
    }

    Event must(Result<Event> r) {
        REQUIRE(static_cast<bool>(r));
        return *r.value;
    }

    std::string user(const std::string& name) {
        return must(ledger.create_user(name, "#D4A373", "", now, TimeSource::Synced)).user_id;
    }

    std::string product(const std::string& name, Cents price) {
        return must(ledger.create_product(name, "Kaffee", "cup", "#D4A373", price, 0, "", now,
                                          TimeSource::Synced))
            .product_id;
    }
};

TEST_CASE("invalid mutation is rejected before persistence") {
    Fixture f;
    auto u = f.user("Ada");
    auto p = f.product("Kaffee", 50);
    f.must(f.ledger.archive_user(u, "arch", f.now, TimeSource::Synced));
    const size_t persisted_before = f.persisted.size();
    auto failed = f.ledger.purchase(u, p, "blocked", f.now, TimeSource::Synced);
    CHECK_FALSE(static_cast<bool>(failed));
    CHECK(f.persisted.size() == persisted_before);
    CHECK(f.ledger.events().back().type == EventType::UserArchived);
}

TEST_CASE("duplicate event id is rejected before persistence") {
    int id_counter = 0;
    Ledger ledger([&id_counter](std::string_view prefix) {
        if (prefix == "evt") return std::string("evt_duplicate");
        return std::string(prefix) + "_" + std::to_string(++id_counter);
    });
    std::vector<Event> persisted;
    ledger.set_commit_hook(CommitHook{[&persisted](const Event& e) -> Result<Unit> {
        persisted.push_back(e);
        return Result<Unit>::ok(Unit{});
    }});

    auto first = ledger.create_user("Ada", "#D4A373", "", f_now(), TimeSource::Synced);
    REQUIRE(static_cast<bool>(first));
    REQUIRE(persisted.size() == 1);
    const uint64_t next_seq = ledger.next_seq();
    const std::string head_hash = ledger.head_hash();

    auto second = ledger.create_user("Bob", "#D4A373", "", f_now(), TimeSource::Synced);
    REQUIRE_FALSE(static_cast<bool>(second));
    REQUIRE(second.error.has_value());
    CHECK(second.error->code == "duplicate");
    CHECK(persisted.size() == 1);
    CHECK(ledger.events().size() == 1);
    CHECK(ledger.users().size() == 1);
    CHECK(ledger.next_seq() == next_seq);
    CHECK(ledger.head_hash() == head_hash);
}

TEST_CASE("leaderboard void outside period does not go negative") {
    Fixture f;
    auto u = f.user("Ada");
    auto p = f.product("Kaffee", 50);
    Timestamp october = 1'697'000'000;
    Timestamp november = october + 31 * 86400;
    auto sale = f.must(f.ledger.purchase(u, p, "p", october, TimeSource::Synced));
    f.must(f.ledger.void_event(sale.id, "late correction", "v", november, TimeSource::Synced));
    auto oct = f.ledger.leaderboard(october - 86400, october + 86400);
    auto nov = f.ledger.leaderboard(november - 86400, november + 86400);
    CHECK(oct.entries[0].count == 1);
    CHECK(nov.entries[0].count == 0);
    CHECK(nov.guest_count == 0);
}

TEST_CASE("generated ids are 64-bit and unique") {
    Ledger ledger;
    std::vector<std::string> ids;
    for (int i = 0; i < 200; ++i) {
        auto e =
            ledger.create_user("U" + std::to_string(i), "#fff", "", f_now(), TimeSource::Synced);
        REQUIRE(static_cast<bool>(e));
        ids.push_back(e.value->user_id);
        CHECK(e.value->user_id.size() == std::string("usr_").size() + 16);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}

TEST_CASE("void actor is preserved") {
    Fixture f;
    auto u = f.user("Ada");
    auto p = f.product("Kaffee", 50);
    auto sale = f.must(f.ledger.purchase(u, p, "p", f.now, TimeSource::Synced));
    auto undo = f.must(f.ledger.void_event(sale.id, "undo", "v", f.now, TimeSource::Synced, "ui"));
    CHECK(undo.actor == "ui");
}

TEST_CASE("topup and guest sale voids reverse cash") {
    Fixture f;
    auto u = f.user("Ada");
    auto p = f.product("Kaffee", 50);
    auto top = f.must(f.ledger.topup(u, 500, "cash", "t", f.now, TimeSource::Synced));
    auto guest = f.must(f.ledger.guest_sale(p, "g", f.now, TimeSource::Synced));
    CHECK(f.ledger.summary().cash_expected == 550);
    f.must(f.ledger.void_event(top.id, "no cash", "vt", f.now, TimeSource::Synced));
    f.must(f.ledger.void_event(guest.id, "no guest", "vg", f.now, TimeSource::Synced));
    CHECK(f.ledger.summary().cash_expected == 0);
    CHECK(f.ledger.summary().topups == 0);
    CHECK(f.ledger.summary().guest_sales == 0);
    CHECK(f.ledger.balance(u) == 0);
}

}  // namespace

TEST_CASE("sha256 known vectors") {
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex("The quick brown fox jumps over the lazy dog") ==
          "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST_CASE("user purchase and balance") {
    Fixture f;
    auto u = f.user("Ada");
    auto p = f.product("Kaffee", 50);
    CHECK(f.ledger.balance(u) == 0);

    auto sale = f.must(f.ledger.purchase(u, p, "req-1", f.now, TimeSource::Synced));
    CHECK(sale.amount_cents == -50);
    CHECK(f.ledger.balance(u) == -50);
    CHECK(f.ledger.summary().debts == 50);
    CHECK(f.ledger.summary().member_sales == 50);
    CHECK(f.ledger.summary().member_items == 1);
    CHECK(f.ledger.summary().cash_expected == 0);
    CHECK(f.persisted.size() == 3);
}

TEST_CASE("idempotent purchase") {
    Fixture f;
    auto u = f.user("Ada");
    auto p = f.product("Kaffee", 50);
    auto a = f.must(f.ledger.purchase(u, p, "same", f.now, TimeSource::Synced));
    auto b = f.must(f.ledger.purchase(u, p, "same", f.now + 5, TimeSource::Synced));
    CHECK(a.id == b.id);
    CHECK(f.ledger.balance(u) == -50);
    CHECK(f.ledger.events().size() == 3);
}

TEST_CASE("guest sale is cash and not a balance") {
    Fixture f;
    auto p = f.product("Kaffee", 50);
    f.must(f.ledger.guest_sale(p, "g1", f.now, TimeSource::Synced));
    auto s = f.ledger.summary();
    CHECK(s.cash_expected == 50);
    CHECK(s.guest_sales == 50);
    CHECK(s.guest_items == 1);
    CHECK(s.debts == 0);
}

TEST_CASE("topup and void") {
    Fixture f;
    auto u = f.user("Ada");
    auto p = f.product("Kaffee", 50);
    auto sale = f.must(f.ledger.purchase(u, p, "p1", f.now, TimeSource::Synced));
    f.must(f.ledger.topup(u, 1000, "cash", "t1", f.now, TimeSource::Synced));
    CHECK(f.ledger.balance(u) == 950);
    CHECK(f.ledger.summary().cash_expected == 1000);

    f.must(f.ledger.void_event(sale.id, "mistake", "v1", f.now, TimeSource::Synced));
    CHECK(f.ledger.balance(u) == 1000);
    CHECK(f.ledger.summary().member_sales == 0);
    CHECK(f.ledger.is_voided(sale.id));
    CHECK_FALSE(f.ledger.void_event(sale.id, "again", "v2", f.now, TimeSource::Synced));
}

TEST_CASE("expense create edit void affects cash") {
    Fixture f;
    auto exp = f.must(f.ledger.add_expense("Maschine", "Kaffeemaschine", "Shop", 45000, "", "e1",
                                           f.now, TimeSource::Synced));
    CHECK(f.ledger.summary().cash_expected == -45000);
    CHECK(f.ledger.summary().expenses == 45000);

    f.must(f.ledger.update_expense(exp.expense_id, "Maschine", "Kaffeemaschine", "Shop", 40000,
                                   "Rabatt", "e2", f.now, TimeSource::Synced));
    CHECK(f.ledger.summary().cash_expected == -40000);
    CHECK(f.ledger.summary().expenses == 40000);

    f.must(f.ledger.void_expense(exp.expense_id, "retour", "e3", f.now, TimeSource::Synced));
    CHECK(f.ledger.summary().cash_expected == 0);
    CHECK(f.ledger.summary().expenses == 0);
}

TEST_CASE("leaderboard counts net purchases by month") {
    Fixture f;
    auto a = f.user("Ada");
    auto b = f.user("Bob");
    auto p = f.product("Kaffee", 50);
    f.now = 1'700'000'000;  // Nov 2023
    auto s1 = f.must(f.ledger.purchase(a, p, "1", f.now, TimeSource::Synced));
    f.must(f.ledger.purchase(a, p, "2", f.now + 10, TimeSource::Synced));
    f.must(f.ledger.purchase(b, p, "3", f.now + 20, TimeSource::Synced));
    f.must(f.ledger.guest_sale(p, "4", f.now + 30, TimeSource::Synced));
    f.must(f.ledger.void_event(s1.id, "oops", "5", f.now + 40, TimeSource::Synced));

    auto l = f.ledger.leaderboard(f.now - 86400, f.now + 86400);
    REQUIRE(l.entries.size() == 2);
    CHECK(l.entries[0].name == "Ada");
    CHECK(l.entries[0].count == 1);
    CHECK(l.entries[1].name == "Bob");
    CHECK(l.entries[1].count == 1);
    CHECK(l.guest_count == 1);
}

TEST_CASE("hash chain rejects tampering") {
    Fixture f;
    auto u = f.user("Ada");
    auto p = f.product("Kaffee", 50);
    f.must(f.ledger.purchase(u, p, "1", f.now, TimeSource::Synced));

    auto events = f.ledger.events();
    Event tampered = events.back();
    tampered.amount_cents = -999;
    Ledger copy([i = 0](std::string_view prefix) mutable {
        return std::string(prefix) + "_" + std::to_string(++i);
    });
    for (size_t i = 0; i + 1 < events.size(); ++i) {
        REQUIRE(static_cast<bool>(copy.apply(events[i])));
    }
    CHECK_FALSE(static_cast<bool>(copy.apply(tampered)));
}

TEST_CASE("product deletion preserves history snapshots") {
    Fixture f;
    auto u = f.user("Ada");
    auto p = f.product("Kaffee", 50);
    f.must(f.ledger.purchase(u, p, "1", f.now, TimeSource::Synced));
    f.must(f.ledger.delete_product(p, "d1", f.now, TimeSource::Synced));
    CHECK(f.ledger.products().empty());
    auto history = f.ledger.events();
    CHECK(history.back().product_name == "Kaffee");
    CHECK(f.ledger.balance(u) == -50);
}

TEST_CASE("settings json round trip") {
    Settings s;
    s.display.rotation = 90;
    s.display.brightness_active = 80;
    s.network.ap_ssid = "Kaffeekasse-Test";
    s.app.guest_enabled = false;
    auto j = settings_to_json(s, false);
    CHECK_FALSE(j["network"].contains("ap_password"));
    auto parsed = settings_from_json(j);
    CHECK(parsed.display.rotation == 90);
    CHECK(parsed.display.brightness_active == 80);
    CHECK(parsed.network.ap_ssid == "Kaffeekasse-Test");
    CHECK(parsed.app.guest_enabled == false);
}

TEST_CASE("user sort setting round trips and rejects unknown spellings") {
    Settings s;
    s.app.user_sort = UserSortOrder::Usage;
    const auto j = settings_to_json(s, false);
    CHECK(j["app"]["user_sort"] == "usage");
    CHECK(settings_from_json(j).app.user_sort == UserSortOrder::Usage);

    // An old or hand-edited blob keeps the base value instead of failing.
    auto broken = j;
    broken["app"]["user_sort"] = "alphabetical";
    Settings base;
    CHECK(base.app.user_sort == UserSortOrder::Usage);
    CHECK(settings_from_json(broken, base).app.user_sort == UserSortOrder::Usage);

    // A blob without the key keeps whatever the caller passed in (the default
    // for a fresh device is "usage", see AppSettings).
    auto legacy = j;
    legacy["app"].erase("user_sort");
    CHECK(settings_from_json(legacy, base).app.user_sort == UserSortOrder::Usage);
    Settings creation_base;
    creation_base.app.user_sort = UserSortOrder::Creation;
    CHECK(settings_from_json(legacy, creation_base).app.user_sort == UserSortOrder::Creation);
}

TEST_CASE("avatar choices persist through user events and leave balances unchanged") {
    Fixture f;
    const auto id = f.user("Ada");
    REQUIRE(f.ledger.find_user(id).has_value());
    CHECK(f.ledger.find_user(id)->avatar_id == 0);

    const auto source = TimeSource::Synced;
    auto selected = f.ledger.update_user(id, "Ada", "#fff", true, "avatar", f.now, source, 30);
    REQUIRE(static_cast<bool>(selected));
    CHECK(selected.value->user_avatar_id == 30);
    CHECK(f.ledger.find_user(id)->avatar_id == 30);
    CHECK(f.ledger.balance(id) == 0);

    // Existing callers do not pass avatar_id, so updates must preserve it.
    f.must(f.ledger.update_user(id, "Ada Renamed", "#fff", true, "rename", f.now, source));
    CHECK(f.ledger.find_user(id)->avatar_id == 30);
    f.must(f.ledger.archive_user(id, "archive", f.now, TimeSource::Synced));
    CHECK(f.ledger.find_user(id)->avatar_id == 30);

    f.must(f.ledger.update_user(id, "Ada", "#fff", true, "clear", f.now, source, 0));
    CHECK(f.ledger.find_user(id)->avatar_id == 0);
}

TEST_CASE("invalid avatar ids do not enter the journal") {
    Fixture f;
    auto id = f.user("Ada");
    const size_t old_events = f.persisted.size();
    const auto source = TimeSource::Synced;
    auto invalid = f.ledger.update_user(id, "Ada", "#fff", true, "invalid", f.now, source, 31);
    CHECK_FALSE(static_cast<bool>(invalid));
    CHECK(f.persisted.size() == old_events);
    CHECK(f.ledger.find_user(id)->avatar_id == 0);
}

TEST_CASE("legacy user events keep their canonical hashes and JSON defaults") {
    Fixture f;
    auto user_id = f.user("Ada");
    REQUIRE(f.ledger.find_user(user_id).has_value());
    const Event& event = f.persisted.front();
    auto json = event_to_json(event);
    CHECK_FALSE(json.contains("user_avatar_id"));

    auto restored = event_from_json(json);
    CHECK(restored.user_avatar_id == 0);
    CHECK(Ledger::make_event_hash(restored) == event.hash);
    CHECK(user_from_json(user_to_json(*f.ledger.find_user(user_id))).avatar_id == 0);
}
