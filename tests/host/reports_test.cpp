#include <doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kaffee/domain.hpp"
#include "kaffee/reports.hpp"

using namespace kaffee;
using namespace kaffee::reports;

namespace {

// Europe/Berlin wall-clock instants used by the fixture.
constexpr Timestamp OCT_01 = 1790805600;       // 2026-10-01 00:00 CEST
constexpr Timestamp OCT_15 = 1792051200;       // 2026-10-15 10:00 CEST
constexpr Timestamp OCT_20 = 1792490400;       // 2026-10-20 11:00 CEST
constexpr Timestamp NOV_01 = 1793487600;       // 2026-11-01 00:00 CET (period start)
constexpr Timestamp NOV_02 = 1793606400;       // 2026-11-02 09:00 CET
constexpr Timestamp NOV_03 = 1793692800;       // 2026-11-03 09:00 CET
constexpr Timestamp NOV_04 = 1793786400;       // 2026-11-04 11:00 CET
constexpr Timestamp NOV_05 = 1793876400;       // 2026-11-05 12:00 CET
constexpr Timestamp NOV_06 = 1793950200;       // 2026-11-06 08:30 CET
constexpr Timestamp NOV_07 = 1794056400;       // 2026-11-07 14:00 CET
constexpr Timestamp NOV_08 = 1794146400;       // 2026-11-08 15:00 CET
constexpr Timestamp NOV_09 = 1794236400;       // 2026-11-09 16:00 CET
constexpr Timestamp NOV_10 = 1794326400;       // 2026-11-10 17:00 CET
constexpr Timestamp NOV_11 = 1794412800;       // 2026-11-11 17:00 CET
constexpr Timestamp NOV_15 = 1794762000;       // 2026-11-15 18:00 CET
constexpr Timestamp NOV_16 = 1794816900;       // 2026-11-16 09:15 CET
constexpr Timestamp NOV_17 = 1794905100;       // 2026-11-17 09:45 CET
constexpr Timestamp NOV_18 = 1794996000;       // 2026-11-18 11:00 CET
constexpr Timestamp NOV_21 = 1795262400;       // 2026-11-21 13:00 CET
constexpr Timestamp NOV_22 = 1795352400;       // 2026-11-22 14:00 CET
constexpr Timestamp NOV_23 = 1795442400;       // 2026-11-23 15:00 CET
constexpr Timestamp NOV_30 = 1796068800;       // 2026-11-30 21:00 CET
constexpr Timestamp DEC_01 = 1796079600;       // 2026-12-01 00:00 CET (period end)
constexpr Timestamp DEC_02 = 1796202000;       // 2026-12-02 10:00 CET
constexpr Timestamp DEC_31 = 1798757940;       // 2026-12-31 23:59 CET
constexpr Timestamp JAN_04_2027 = 1799017200;  // 2027-01-04 00:00 CET

struct Fixture {
    std::shared_ptr<int> counter = std::make_shared<int>(0);
    Ledger ledger;

    Fixture() {
        ledger = Ledger([counter = counter](std::string_view prefix) {
            return std::string(prefix) + "_" + std::to_string(++*counter);
        });
    }

    Event must(Result<Event> result) {
        REQUIRE(static_cast<bool>(result));
        return *result.value;
    }

    std::string user(const std::string& name, Timestamp ts) {
        return must(ledger.create_user(name, "#D4A373", "", ts, TimeSource::Synced)).user_id;
    }

    std::string product(const std::string& name, Cents price, Timestamp ts) {
        return must(ledger.create_product(name, name, "cup", "#D4A373", price, 0, "", ts,
                                          TimeSource::Synced))
            .product_id;
    }
};

struct Scenario : Fixture {
    std::string ada;
    std::string ben;
    std::string cem;
    std::string kaffee;
    std::string tee;
    Event october_purchase;
    Event ben_topup;

    Scenario() {
        ada = user("Ada", OCT_01);
        ben = user("Ben", OCT_01);
        cem = user("Cem", OCT_01);
        kaffee = product("Kaffee", 50, OCT_01);
        tee = product("Tee", 80, OCT_01);

        october_purchase = must(ledger.purchase(ada, kaffee, "r-oct", OCT_15, TimeSource::Synced));
        must(ledger.topup(ben, 1000, "Startgeld", "r-start", OCT_20, TimeSource::Synced));

        must(ledger.topup(ada, 1000, "", "r-1", NOV_02, TimeSource::Synced));
        must(ledger.purchase(ben, tee, "r-2", NOV_04, TimeSource::Synced));
        must(ledger.guest_sale(kaffee, "r-3", NOV_05, TimeSource::Synced));
        auto milch = must(ledger.add_expense("Einkauf", "Milch", "Rewe", 300, "", "r-4", NOV_06,
                                             TimeSource::Synced));
        must(ledger.adjust(ada, -100, "Schwund", "r-5", NOV_07, TimeSource::Synced));
        must(ledger.topup(cem, 500, "", "r-6", NOV_08, TimeSource::Synced));
        must(ledger.void_event(october_purchase.id, "Fehlbuchung", "r-7", NOV_09,
                               TimeSource::Synced));
        ben_topup = must(ledger.topup(ben, 200, "", "r-8", NOV_10, TimeSource::Synced));
        must(ledger.archive_user(cem, "r-9", NOV_15, TimeSource::Synced));
        auto ada_tee = must(ledger.purchase(ada, tee, "r-10", NOV_16, TimeSource::Synced));
        must(ledger.void_event(ada_tee.id, "Korrektur", "r-11", NOV_17, TimeSource::Synced));
        must(ledger.guest_sale(tee, "r-12", NOV_18, TimeSource::Synced));
        auto milch_update =
            must(ledger.update_expense(milch.expense_id, "Einkauf", "Milch", "Rewe", 400, "",
                                       "r-13", NOV_21, TimeSource::Synced));
        (void)milch_update;
        auto service = must(ledger.add_expense("Service", "Maschine", "Service24", 800, "", "r-14",
                                               NOV_22, TimeSource::Synced));
        must(ledger.void_expense(service.expense_id, "Falsch gebucht", "r-15", NOV_23,
                                 TimeSource::Synced));
        must(ledger.record_cash_count(1500, "Kassensturz", "r-16", NOV_30, TimeSource::Synced));
        must(ledger.void_event(ben_topup.id, "Rückzahlung", "r-17", DEC_02, TimeSource::Synced));
    }
};

std::string render(const Ledger& ledger, ReportKind kind, const Period& period, CsvFormat format) {
    std::string out;
    Sink sink{[&out](std::string_view chunk) {
        out.append(chunk.data(), chunk.size());
        return true;
    }};
    auto result = write_csv(ledger, kind, period, format, sink);
    REQUIRE(static_cast<bool>(result));
    return out;
}

std::vector<std::vector<Value>> collect_rows(const Ledger& ledger, ReportKind kind,
                                             const Period& period) {
    std::vector<std::vector<Value>> rows;
    auto result = for_each_row(ledger, kind, period, [&rows](const std::vector<Value>& row) {
        rows.push_back(row);
        return true;
    });
    REQUIRE(static_cast<bool>(result));
    return rows;
}

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string golden_path(std::string_view name) {
    return std::string(GOLDEN_DIR) + "/reports/" + std::string(name);
}

void check_golden(const std::string& name, const std::string& actual) {
    const std::string path = golden_path(name);
    if (std::getenv("UPDATE_GOLDEN") != nullptr) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(actual.data(), static_cast<std::streamsize>(actual.size()));
        return;
    }

    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing golden file: " << path);
    std::string expected((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (actual != expected) {
        const std::string actual_path = "/tmp/opencode/actual_" + name;
        std::filesystem::create_directories(std::filesystem::path(actual_path).parent_path());
        std::ofstream out(actual_path, std::ios::binary);
        out.write(actual.data(), static_cast<std::streamsize>(actual.size()));
        MESSAGE("actual output written to " << actual_path);
    }
    CHECK(actual == expected);
}

}  // namespace

TEST_CASE("period helpers use Europe/Berlin and ISO weeks") {
    const Period week_of_november = week(NOV_02);
    CHECK(week_of_november.from == 1793574000);  // Monday 2026-11-02 00:00 CET
    CHECK(week_of_november.to == 1794178800);    // Monday 2026-11-09 00:00 CET
    CHECK(week_of_november.kind == PeriodKind::Week);

    const Period november = month(NOV_02);
    CHECK(november.from == NOV_01);
    CHECK(november.to == DEC_01);

    const Period year_2026 = year(NOV_02);
    CHECK(year_2026.from == 1767222000);  // 2026-01-01 00:00 CET
    CHECK(year_2026.to == 1798758000);    // 2027-01-01 00:00 CET

    const Period everything = all();
    CHECK(everything.from == 0);
    CHECK(everything.to == std::numeric_limits<Timestamp>::max());

    CHECK(contains(november, NOV_01));
    CHECK_FALSE(contains(november, DEC_01));
    CHECK_FALSE(valid(custom(DEC_01, NOV_01)));
    CHECK(valid(custom(NOV_01, DEC_01)));
}

TEST_CASE("ISO week crossing the year boundary") {
    const Period new_year_week = week(DEC_31);
    CHECK(new_year_week.from == 1798412400);  // Monday 2026-12-28 00:00 CET
    CHECK(new_year_week.to == JAN_04_2027);   // Monday 2027-01-04 00:00 CET
    CHECK(new_year_week.kind == PeriodKind::Week);

    // 2026-12-31 belongs to ISO week 53 of 2026, not to 2027.
    CHECK(report_filename(ReportKind::Overview, new_year_week) ==
          "kaffeekasse_2026-W53_overview.csv");
    CHECK(report_filename(ReportKind::Overview, year(DEC_31)) == "kaffeekasse_2026_overview.csv");
    CHECK(report_filename(ReportKind::Overview, year(JAN_04_2027)) ==
          "kaffeekasse_2027_overview.csv");
}

TEST_CASE("DST transitions shift period boundaries") {
    // 2026-03-29 01:00 UTC: CET -> CEST (last Sunday of March).
    const Timestamp spring_utc = 1774778400;
    CHECK(berlin_is_dst(spring_utc));
    CHECK(berlin_utc_offset(spring_utc) == 7200);
    Civil spring = to_berlin(spring_utc);
    CHECK(spring.year == 2026);
    CHECK(spring.month == 3);
    CHECK(spring.day == 29);
    CHECK(spring.hour == 12);
    CHECK(spring.weekday == 7);

    // 2026-10-25 01:00 UTC: CEST -> CET (last Sunday of October).
    const Timestamp autumn_utc = 1792926000;  // 2026-10-25 12:00 Berlin
    Civil autumn = to_berlin(autumn_utc);
    CHECK(autumn.year == 2026);
    CHECK(autumn.month == 10);
    CHECK(autumn.day == 25);
    CHECK(autumn.hour == 12);
    CHECK(autumn.weekday == 7);
    CHECK_FALSE(berlin_is_dst(1792969200));  // 2026-10-26 00:00 CET

    // The ISO week crossing the spring transition is one hour shorter.
    const Period spring_week = week(spring_utc);
    CHECK(spring_week.from == 1774220400);  // Monday 2026-03-23 00:00 CET
    CHECK(spring_week.to == 1774821600);    // Monday 2026-03-30 00:00 CEST
    CHECK(spring_week.to - spring_week.from == 601200);

    // The week crossing the autumn transition is one hour longer.
    const Period autumn_week = week(autumn_utc);
    CHECK(autumn_week.from == 1792360800);  // Monday 2026-10-19 00:00 CEST
    CHECK(autumn_week.to == 1792969200);    // Monday 2026-10-26 00:00 CET
    CHECK(autumn_week.to - autumn_week.from == 608400);

    // March 2026 has 31 days minus the DST hour.
    const Period march = month(1774778400);
    CHECK(march.from == 1772319600);
    CHECK(march.to == 1774994400);
    CHECK(march.to - march.from == 2674800);

    CHECK(from_berlin(Civil{2026, 3, 23, 0, 0, 0}) == 1774220400);
    CHECK(from_berlin(Civil{2026, 10, 26, 0, 0, 0}) == 1792969200);
}

TEST_CASE("period and report names round-trip") {
    CHECK(report_kind_name(ReportKind::Cashflow) == "cashflow");
    CHECK(report_kind_from_name("transactions") == ReportKind::Transactions);
    CHECK_FALSE(report_kind_from_name("nope").has_value());
    CHECK(period_kind_name(PeriodKind::Week) == "week");
    CHECK(period_kind_from_name("custom") == PeriodKind::Custom);
    CHECK(period(PeriodKind::Custom, NOV_02).kind == PeriodKind::Custom);
    CHECK_FALSE(valid(period(PeriodKind::Custom, NOV_02)));
}

TEST_CASE("CSV primitives: money, dates, escaping, injection") {
    CHECK(format_money(0, CsvFormat::Excel) == "0,00");
    CHECK(format_money(5, CsvFormat::Excel) == "0,05");
    CHECK(format_money(150, CsvFormat::Excel) == "1,50");
    CHECK(format_money(-150, CsvFormat::Excel) == "-1,50");
    CHECK(format_money(123456, CsvFormat::Excel) == "1234,56");
    CHECK(format_money(-150, CsvFormat::Raw) == "-1.50");
    CHECK(format_money(std::numeric_limits<Cents>::min(), CsvFormat::Excel) ==
          "-92233720368547758,08");

    CHECK(format_date(NOV_02, CsvFormat::Excel) == "02.11.2026");
    CHECK(format_date(NOV_02, CsvFormat::Raw) == "2026-11-02");
    CHECK(format_datetime(NOV_02, CsvFormat::Excel) == "02.11.2026 09:00:00");
    CHECK(format_datetime(NOV_02, CsvFormat::Raw) == "2026-11-02T09:00:00+01:00");
    CHECK(format_datetime(1774778400, CsvFormat::Raw) == "2026-03-29T12:00:00+02:00");

    CHECK(escape_field("plain", CsvFormat::Excel) == "plain");
    CHECK(escape_field("a;b", CsvFormat::Excel) == "\"a;b\"");
    CHECK(escape_field("a;b", CsvFormat::Raw) == "a;b");
    CHECK(escape_field("a,b", CsvFormat::Raw) == "\"a,b\"");
    CHECK(escape_field("a\"b", CsvFormat::Raw) == "\"a\"\"b\"");
    CHECK(escape_field("a\nb", CsvFormat::Excel) == "\"a\nb\"");

    CHECK(protect_formula("=SUM(A1)") == "'=SUM(A1)");
    CHECK(protect_formula("+cmd") == "'+cmd");
    CHECK(protect_formula("@import") == "'@import");
    CHECK(protect_formula("-12,50") == "-12,50");
    CHECK(protect_formula("-1.5e3") == "-1.5e3");
    CHECK(protect_formula("hello") == "hello");
}

TEST_CASE("CSV writer emits Excel and raw framing") {
    std::string excel;
    Sink sink{[&excel](std::string_view chunk) {
        excel.append(chunk.data(), chunk.size());
        return true;
    }};
    const std::vector<Column> table = {
        {"name", "Name", ColumnType::Text},
        {"amount_eur", "Betrag (EUR)", ColumnType::MoneyEur},
        {"amount_cents", "Betrag (Cent)", ColumnType::MoneyCents},
        {"when", "Zeitpunkt", ColumnType::DateTime},
        {"count", "Anzahl", ColumnType::Integer},
        {"flag", "Aktiv", ColumnType::Bool},
    };
    CsvWriter writer(CsvFormat::Excel, sink);
    REQUIRE(writer.begin());
    REQUIRE(writer.header(table));
    REQUIRE(writer.row(table, {Value::of_text("=boese"), Value::money(-150), Value::money(-150),
                               Value::datetime(NOV_02), Value::integer(3), Value::boolean(true)}));
    REQUIRE(writer.end());

    CHECK(excel.rfind("\xEF\xBB\xBFsep=;\r\n", 0) == 0);
    CHECK(contains(excel, "Name;Betrag (EUR);Betrag (Cent);Zeitpunkt;Anzahl;Aktiv\r\n"));
    CHECK(contains(excel, "'=boese;-1,50;-150;02.11.2026 09:00:00;3;ja\r\n"));

    std::string raw;
    Sink raw_sink{[&raw](std::string_view chunk) {
        raw.append(chunk.data(), chunk.size());
        return true;
    }};
    CsvWriter raw_writer(CsvFormat::Raw, raw_sink);
    REQUIRE(raw_writer.begin());
    REQUIRE(raw_writer.header(table));
    REQUIRE(
        raw_writer.row(table, {Value::of_text("=boese"), Value::money(-150), Value::money(-150),
                               Value::datetime(NOV_02), Value::integer(3), Value::boolean(true)}));
    REQUIRE(raw_writer.end());
    CHECK(raw.rfind("name,amount_eur", 0) == 0);
    CHECK(contains(raw, "=boese,-1.50,-150,2026-11-02T09:00:00+01:00,3,true\n"));
}

TEST_CASE("write_csv rejects invalid periods and aborted sinks") {
    Fixture f;
    std::string out;
    Sink sink{[&out](std::string_view chunk) {
        out.append(chunk.data(), chunk.size());
        return true;
    }};
    auto invalid =
        write_csv(f.ledger, ReportKind::Overview, custom(DEC_01, NOV_01), CsvFormat::Excel, sink);
    CHECK_FALSE(static_cast<bool>(invalid));
    CHECK(invalid.error->code == "period");

    Sink aborted{[](std::string_view) { return false; }};
    auto failed =
        write_csv(f.ledger, ReportKind::Overview, month(NOV_02), CsvFormat::Excel, aborted);
    CHECK_FALSE(static_cast<bool>(failed));
    CHECK(failed.error->code == "sink");
}

TEST_CASE("mid-stream sink failure aborts the report") {
    Scenario s;
    size_t calls = 0;
    Sink sink{[&calls](std::string_view) { return ++calls < 5; }};
    auto result =
        write_csv(s.ledger, ReportKind::Transactions, month(NOV_02), CsvFormat::Excel, sink);
    CHECK_FALSE(static_cast<bool>(result));
    CHECK(result.error->code == "sink");
    CHECK(calls == 5);
}

TEST_CASE("voids are attributed to the original booking period") {
    Scenario s;
    const Period october = month(OCT_15);
    const Period november = month(NOV_02);
    const Period december = month(DEC_02);

    // The October booking was voided in November. Consumption applies the
    // correction to October, so November has no Ada/Kaffee row at all.
    auto october_consumption = collect_rows(s.ledger, ReportKind::Consumption, october);
    const std::vector<Value>* ada_kaffee = nullptr;
    for (const auto& row : october_consumption) {
        if (row[0].text == "Mitglied" && row[1].text == "Ada" && row[2].text == "Kaffee")
            ada_kaffee = &row;
    }
    REQUIRE(ada_kaffee != nullptr);
    CHECK((*ada_kaffee)[4].number == 0);
    CHECK((*ada_kaffee)[5].cents == 0);
    CHECK((*ada_kaffee)[7].number == 1);

    auto consumption = collect_rows(s.ledger, ReportKind::Consumption, november);
    for (const auto& row : consumption) {
        CHECK_FALSE((row[0].text == "Mitglied" && row[1].text == "Ada" && row[2].text == "Kaffee"));
    }

    // A purchase voided inside the same period nets to zero but keeps the
    // correction marker.
    const std::vector<Value>* ada_tee = nullptr;
    for (const auto& row : consumption) {
        if (row[0].text == "Mitglied" && row[1].text == "Ada" && row[2].text == "Tee")
            ada_tee = &row;
    }
    REQUIRE(ada_tee != nullptr);
    CHECK((*ada_tee)[4].number == 0);
    CHECK((*ada_tee)[5].cents == 0);
    CHECK((*ada_tee)[7].number == 1);

    const std::vector<Value>* ben_tee = nullptr;
    for (const auto& row : consumption) {
        if (row[0].text == "Mitglied" && row[1].text == "Ben" && row[2].text == "Tee")
            ben_tee = &row;
    }
    REQUIRE(ben_tee != nullptr);
    CHECK((*ben_tee)[4].number == 1);
    CHECK((*ben_tee)[5].cents == 80);

    // Deposits remain void-month cash movements: Ben's November top-up is
    // still listed even though it is voided in December.
    auto deposits_nov = collect_rows(s.ledger, ReportKind::Deposits, november);
    const std::vector<Value>* ben_deposit = nullptr;
    for (const auto& row : deposits_nov) {
        if (row[0].text == "Ben") ben_deposit = &row;
    }
    REQUIRE(ben_deposit != nullptr);
    CHECK((*ben_deposit)[1].number == 1);
    CHECK((*ben_deposit)[2].cents == 200);
    CHECK((*ben_deposit)[4].number == 0);

    auto deposits_dec = collect_rows(s.ledger, ReportKind::Deposits, december);
    const std::vector<Value>* ben_december = nullptr;
    for (const auto& row : deposits_dec) {
        if (row[0].text == "Ben") ben_december = &row;
    }
    REQUIRE(ben_december != nullptr);
    CHECK((*ben_december)[1].number == -1);
    CHECK((*ben_december)[2].cents == -200);
    CHECK((*ben_december)[4].number == 1);

    // Transactions mark both the reversed original and the void.
    std::string transactions =
        render(s.ledger, ReportKind::Transactions, november, CsvFormat::Excel);
    CHECK(contains(transactions, ";storniert;"));
    CHECK(contains(transactions, ";Storno;"));
}

TEST_CASE("cash flow and balances reconcile with the ledger") {
    Scenario s;
    const Period november = month(NOV_02);

    auto cashflow = collect_rows(s.ledger, ReportKind::Cashflow, november);
    REQUIRE(cashflow.size() == 5);
    CHECK(cashflow[0][0].text == "Anfangssaldo Kasse");
    CHECK(cashflow[0][1].cents == 1000);
    CHECK(cashflow[1][0].text == "Einzahlungen (Mitglieder)");
    CHECK(cashflow[1][1].cents == 1700);
    CHECK(cashflow[2][0].text == "Gastverkäufe");
    CHECK(cashflow[2][1].cents == 130);
    CHECK(cashflow[3][0].text == "Ausgaben");
    CHECK(cashflow[3][1].cents == -400);
    CHECK(cashflow[4][0].text == "Endsaldo Kasse");
    CHECK(cashflow[4][1].cents == 2430);

    auto balances = collect_rows(s.ledger, ReportKind::Balances, november);
    const std::vector<Value>* ada = nullptr;
    const std::vector<Value>* ben = nullptr;
    const std::vector<Value>* cem = nullptr;
    const std::vector<Value>* total = nullptr;
    for (const auto& row : balances) {
        if (row[0].text == "Ada") ada = &row;
        if (row[0].text == "Ben") ben = &row;
        if (row[0].text == "Cem") cem = &row;
        if (row[0].text == "Summe") total = &row;
    }
    REQUIRE(ada != nullptr);
    REQUIRE(ben != nullptr);
    REQUIRE(cem != nullptr);
    REQUIRE(total != nullptr);
    CHECK((*ada)[1].cents == 900);
    CHECK((*ben)[1].cents == 1120);
    CHECK((*cem)[1].cents == 500);
    CHECK((*cem)[7].text == "archiviert");
    CHECK((*total)[1].cents == 2520);
    CHECK((*total)[3].cents == 2520);
    CHECK((*total)[5].cents == 0);

    // Balances and status are reconstructed as of the period end. Cem is
    // still active in October and the November void is not applied yet.
    auto october_balances = collect_rows(s.ledger, ReportKind::Balances, month(OCT_15));
    const std::vector<Value>* october_ada = nullptr;
    const std::vector<Value>* october_cem = nullptr;
    const std::vector<Value>* october_total = nullptr;
    for (const auto& row : october_balances) {
        if (row[0].text == "Ada") october_ada = &row;
        if (row[0].text == "Cem") october_cem = &row;
        if (row[0].text == "Summe") october_total = &row;
    }
    REQUIRE(october_ada != nullptr);
    REQUIRE(october_cem != nullptr);
    REQUIRE(october_total != nullptr);
    CHECK((*october_ada)[1].cents == -50);
    CHECK((*october_ada)[5].cents == 50);
    CHECK((*october_cem)[1].cents == 0);
    CHECK((*october_cem)[7].text == "aktiv");
    CHECK((*october_total)[1].cents == 950);
    CHECK((*october_total)[3].cents == 1000);
    CHECK((*october_total)[5].cents == 50);
}

TEST_CASE("overview and leaderboard reflect the period") {
    Scenario s;
    const Period november = month(NOV_02);

    auto overview = collect_rows(s.ledger, ReportKind::Overview, november);
    REQUIRE(overview.size() == 11);
    CHECK(overview[0][0].text == "Mitglieder: Artikel");
    CHECK(overview[0][1].number == 1);
    CHECK(overview[0][2].cents == 80);
    CHECK(overview[1][0].text == "Gastverkäufe: Artikel");
    CHECK(overview[1][1].number == 2);
    CHECK(overview[1][2].cents == 130);
    CHECK(overview[2][0].text == "Einzahlungen");
    CHECK(overview[2][1].number == 2);
    CHECK(overview[2][2].cents == 1500);
    CHECK(overview[3][0].text == "Ausgaben (Belege)");
    CHECK(overview[3][1].number == 1);
    CHECK(overview[3][2].cents == 400);
    CHECK(overview[4][0].text == "Geschätzte Zeitangaben");
    CHECK(overview[4][1].number == 0);
    CHECK(overview[6][2].cents == 2430);  // closing cash
    CHECK(overview[7][2].cents == 2520);  // prepaid
    CHECK(overview[8][2].cents == 0);     // debts
    CHECK(overview[9][2].cents == 2520);  // net liabilities
    CHECK(overview[10][2].cents == -90);  // equity

    Leaderboard board = leaderboard(s.ledger, november);
    CHECK(board.guest_count == 2);
    REQUIRE_FALSE(board.entries.empty());
    CHECK(board.entries[0].name == "Ben");
    CHECK(board.entries[0].count == 1);
    CHECK(monthly_leaderboard(s.ledger, NOV_02).entries.size() == board.entries.size());
}

TEST_CASE("expense report keeps voided rows visible") {
    Scenario s;
    auto expenses = collect_rows(s.ledger, ReportKind::Expenses, month(NOV_02));
    REQUIRE(expenses.size() == 4);
    CHECK(expenses[0][1].text == "Einkauf");
    CHECK(expenses[0][4].cents == -300);
    CHECK(expenses[0][6].text == "erfasst");
    CHECK(expenses[1][4].cents == -100);
    CHECK(expenses[1][6].text == "geändert");
    CHECK(expenses[2][1].text == "Service");
    CHECK(expenses[2][4].cents == -800);
    CHECK(expenses[2][6].text == "storniert");
    CHECK(expenses[3][4].cents == 800);
    CHECK(expenses[3][6].text == "Storno");
}

TEST_CASE("formula injection protection does not mangle numbers") {
    Fixture f;
    auto user = f.user("=SUM(A1:A2)", OCT_01);
    auto product = f.product("-Tee", 80, OCT_01);
    f.must(f.ledger.topup(user, 500, "@cmd", "i-1", NOV_02, TimeSource::Synced));
    f.must(f.ledger.purchase(user, product, "i-2", NOV_04, TimeSource::Synced));
    f.must(f.ledger.adjust(user, -150, "-12,50", "i-3", NOV_05, TimeSource::Synced));

    std::string excel = render(f.ledger, ReportKind::Transactions, month(NOV_02), CsvFormat::Excel);
    CHECK(contains(excel, "'=SUM(A1:A2)"));
    CHECK(contains(excel, "'-Tee"));
    CHECK(contains(excel, "'@cmd"));
    // A numeric-looking text cell stays numeric (no apostrophe).
    CHECK(contains(excel, ";-12,50\r\n"));
    CHECK(contains(excel, ";-0,80;-80;"));

    std::string raw = render(f.ledger, ReportKind::Transactions, month(NOV_02), CsvFormat::Raw);
    CHECK(contains(raw, "=SUM(A1:A2)"));
    CHECK_FALSE(contains(raw, "'=SUM(A1:A2)"));
}

TEST_CASE("time quality and payment labels are exported") {
    Fixture f;
    auto user = f.user("Ada", OCT_01);
    f.must(f.ledger.topup(user, 500, "", "q-1", NOV_02, TimeSource::Unset));
    f.must(f.ledger.adjust(user, -100, "Korrektur", "q-2", NOV_04, TimeSource::Restored));

    std::string excel = render(f.ledger, ReportKind::Transactions, month(NOV_02), CsvFormat::Excel);
    CHECK(contains(excel, ";Bar;"));
    CHECK(contains(excel, ";unbekannt;"));
    CHECK(contains(excel, ";wiederhergestellt;"));

    auto overview = collect_rows(f.ledger, ReportKind::Overview, month(NOV_02));
    REQUIRE(overview.size() == 11);
    CHECK(overview[4][0].text == "Geschätzte Zeitangaben");
    CHECK(overview[4][1].number == 2);  // top-up (unset) + adjustment (restored)
}

TEST_CASE("CSV is streamed as multiple chunks") {
    Scenario s;
    size_t calls = 0;
    size_t bytes = 0;
    Sink sink{[&calls, &bytes](std::string_view chunk) {
        ++calls;
        bytes += chunk.size();
        return true;
    }};
    auto result =
        write_csv(s.ledger, ReportKind::Transactions, month(NOV_02), CsvFormat::Excel, sink);
    REQUIRE(static_cast<bool>(result));
    CHECK(calls > 10);
    CHECK(bytes > 500);
}

TEST_CASE("report filenames follow the period") {
    CHECK(report_filename(ReportKind::Overview, month(NOV_02)) ==
          "kaffeekasse_2026-11_overview.csv");
    CHECK(report_filename(ReportKind::Overview, week(NOV_02)) ==
          "kaffeekasse_2026-W45_overview.csv");
    CHECK(report_filename(ReportKind::Overview, year(NOV_02)) == "kaffeekasse_2026_overview.csv");
    CHECK(report_filename(ReportKind::Cashflow, all()) == "kaffeekasse_all_cashflow.csv");
    CHECK(report_filename(ReportKind::Balances, custom(NOV_02, NOV_05)) ==
          "kaffeekasse_2026-11-02_2026-11-05_balances.csv");
    CHECK(report_filename(ReportKind::Overview,
                          custom(NOV_02, std::numeric_limits<Timestamp>::max())) ==
          "kaffeekasse_2026-11-02_offen_overview.csv");
    CHECK(std::string(csv_content_type(CsvFormat::Excel)) == "text/csv; charset=utf-8");
}

TEST_CASE("balances exclude users created after period end") {
    Fixture f;
    auto ada = f.user("Ada", OCT_01);
    auto dora = f.user("Dora", NOV_15);
    f.must(f.ledger.topup(ada, 100, "", "c-1", OCT_20, TimeSource::Synced));
    f.must(f.ledger.topup(dora, 50, "", "c-2", NOV_16, TimeSource::Synced));

    auto october = collect_rows(f.ledger, ReportKind::Balances, month(OCT_15));
    for (const auto& row : october) CHECK(row[0].text != "Dora");
    const std::vector<Value>* october_total = nullptr;
    for (const auto& row : october) {
        if (row[0].text == "Summe") october_total = &row;
    }
    REQUIRE(october_total != nullptr);
    CHECK((*october_total)[1].cents == 100);

    auto november = collect_rows(f.ledger, ReportKind::Balances, month(NOV_02));
    const std::vector<Value>* dora_row = nullptr;
    for (const auto& row : november) {
        if (row[0].text == "Dora") dora_row = &row;
    }
    REQUIRE(dora_row != nullptr);
    CHECK((*dora_row)[1].cents == 50);
}

TEST_CASE("negative balances produce debts and signed equity") {
    Fixture f;
    auto ada = f.user("Ada", OCT_01);
    auto ben = f.user("Ben", OCT_01);
    auto product = f.product("Kaffee", 150, OCT_01);

    // Ada buys before paying; her top-up happens after the reported period.
    f.must(f.ledger.purchase(ada, product, "d-1", NOV_02, TimeSource::Synced));
    f.must(f.ledger.topup(ben, 50, "", "d-2", NOV_04, TimeSource::Synced));
    f.must(f.ledger.topup(ada, 50, "", "d-3", DEC_02, TimeSource::Synced));

    auto balances = collect_rows(f.ledger, ReportKind::Balances, month(NOV_02));
    const std::vector<Value>* ada_row = nullptr;
    const std::vector<Value>* ben_row = nullptr;
    const std::vector<Value>* total = nullptr;
    for (const auto& row : balances) {
        if (row[0].text == "Ada") ada_row = &row;
        if (row[0].text == "Ben") ben_row = &row;
        if (row[0].text == "Summe") total = &row;
    }
    REQUIRE(ada_row != nullptr);
    REQUIRE(ben_row != nullptr);
    REQUIRE(total != nullptr);
    CHECK((*ada_row)[1].cents == -150);
    CHECK((*ada_row)[3].cents == 0);    // prepaid
    CHECK((*ada_row)[5].cents == 150);  // debt
    CHECK((*ben_row)[1].cents == 50);
    CHECK((*ben_row)[3].cents == 50);
    CHECK((*ben_row)[5].cents == 0);
    CHECK((*total)[1].cents == -100);
    CHECK((*total)[3].cents == 50);
    CHECK((*total)[5].cents == 150);

    auto overview = collect_rows(f.ledger, ReportKind::Overview, month(NOV_02));
    REQUIRE(overview.size() == 11);
    CHECK(overview[6][2].cents == 50);    // closing cash
    CHECK(overview[7][2].cents == 50);    // prepaid
    CHECK(overview[8][2].cents == 150);   // debts, positive magnitude
    CHECK(overview[9][2].cents == -100);  // net liabilities
    CHECK(overview[10][2].cents == 150);  // equity = 50 - (-100)

    auto december = collect_rows(f.ledger, ReportKind::Balances, month(DEC_02));
    const std::vector<Value>* ada_december = nullptr;
    for (const auto& row : december) {
        if (row[0].text == "Ada") ada_december = &row;
    }
    REQUIRE(ada_december != nullptr);
    CHECK((*ada_december)[1].cents == -100);
    CHECK((*ada_december)[5].cents == 100);
}

TEST_CASE("guest sale and adjustment voids net in the original period") {
    Fixture f;
    auto user = f.user("Ada", OCT_01);
    auto product = f.product("Kaffee", 50, OCT_01);
    f.must(f.ledger.topup(user, 1000, "", "gv-0", NOV_02, TimeSource::Synced));
    auto guest = f.must(f.ledger.guest_sale(product, "gv-1", NOV_03, TimeSource::Synced));
    f.must(f.ledger.void_event(guest.id, "Gast storniert", "gv-2", NOV_10, TimeSource::Synced));
    auto adjustment =
        f.must(f.ledger.adjust(user, 200, "Bonus", "gv-3", NOV_04, TimeSource::Synced));
    f.must(f.ledger.void_event(adjustment.id, "Bonus zurück", "gv-4", NOV_11, TimeSource::Synced));

    auto consumption = collect_rows(f.ledger, ReportKind::Consumption, month(NOV_02));
    REQUIRE(consumption.size() == 1);
    CHECK(consumption[0][0].text == "Gast");
    CHECK(consumption[0][2].text == "Kaffee");
    CHECK(consumption[0][4].number == 0);
    CHECK(consumption[0][5].cents == 0);
    CHECK(consumption[0][7].number == 1);

    auto overview = collect_rows(f.ledger, ReportKind::Overview, month(NOV_02));
    CHECK(overview[1][1].number == 0);  // guest items
    CHECK(overview[1][2].cents == 0);

    auto balances = collect_rows(f.ledger, ReportKind::Balances, month(NOV_02));
    const std::vector<Value>* ada = nullptr;
    for (const auto& row : balances) {
        if (row[0].text == "Ada") ada = &row;
    }
    REQUIRE(ada != nullptr);
    CHECK((*ada)[1].cents == 1000);  // +200 adjustment and -200 void

    std::string transactions =
        render(f.ledger, ReportKind::Transactions, month(NOV_02), CsvFormat::Excel);
    CHECK(contains(transactions, "Gastverkauf"));
    CHECK(contains(transactions, "Korrektur"));
    CHECK(contains(transactions, ";Storno;"));
}

TEST_CASE("expense void across periods uses the delta log") {
    Fixture f;
    auto expense = f.must(f.ledger.add_expense("Einkauf", "Milch", "Rewe", 500, "", "ev-1", OCT_15,
                                               TimeSource::Synced));
    f.must(f.ledger.void_expense(expense.expense_id, "Falsch gebucht", "ev-2", NOV_05,
                                 TimeSource::Synced));

    auto october_expenses = collect_rows(f.ledger, ReportKind::Expenses, month(OCT_15));
    REQUIRE(october_expenses.size() == 1);
    CHECK(october_expenses[0][4].cents == -500);
    CHECK(october_expenses[0][6].text == "storniert");

    auto november_expenses = collect_rows(f.ledger, ReportKind::Expenses, month(NOV_02));
    REQUIRE(november_expenses.size() == 1);
    CHECK(november_expenses[0][4].cents == 500);
    CHECK(november_expenses[0][6].text == "Storno");

    // Overview booking aggregates follow the original period: a voided expense
    // is removed from October and the reversal is not an expense in November.
    auto october_overview = collect_rows(f.ledger, ReportKind::Overview, month(OCT_15));
    CHECK(october_overview[3][1].number == 0);
    CHECK(october_overview[3][2].cents == 0);
    auto november_overview = collect_rows(f.ledger, ReportKind::Overview, month(NOV_02));
    CHECK(november_overview[3][2].cents == 0);

    // Cash flow stays timestamp based.
    auto october_cash = collect_rows(f.ledger, ReportKind::Cashflow, month(OCT_15));
    CHECK(october_cash[3][1].cents == -500);
    auto november_cash = collect_rows(f.ledger, ReportKind::Cashflow, month(NOV_02));
    CHECK(november_cash[3][1].cents == 500);
}

TEST_CASE("voids of non-financial events are not transaction rows") {
    Fixture f;
    auto user = f.user("Ada", OCT_01);
    auto update =
        f.must(f.ledger.update_user(user, "Ada2", "#D4A373", true, "", NOV_02, TimeSource::Synced));
    f.must(f.ledger.void_event(update.id, "rename undo", "nv-1", NOV_03, TimeSource::Synced));
    f.must(f.ledger.topup(user, 100, "", "nv-2", NOV_04, TimeSource::Synced));

    auto rows = collect_rows(f.ledger, ReportKind::Transactions, month(NOV_02));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0][1].text == "Einzahlung");
}

TEST_CASE("JSON envelope matches the documented report shape") {
    Scenario s;
    const Period november = month(NOV_02);

    const auto payload = to_json(s.ledger, ReportKind::Consumption, november);
    CHECK(payload.at("kind") == "consumption");
    CHECK(payload.at("period") == "month");
    CHECK(payload.at("from") == november.from);
    CHECK(payload.at("to") == november.to);
    REQUIRE(payload.contains("columns"));
    REQUIRE(payload.at("columns").is_array());
    REQUIRE(payload.at("columns").size() == columns(ReportKind::Consumption).size());
    CHECK(payload.at("columns").at(5).at("key") == "amount_eur");
    CHECK(payload.at("columns").at(5).at("type") == "money");
    CHECK(payload.at("columns").at(4).at("type") == "number");

    REQUIRE(payload.contains("rows"));
    REQUIRE(payload.at("rows").is_array());
    CHECK_FALSE(payload.at("rows").empty());
    const auto& row = payload.at("rows").at(0);
    CHECK(row.contains("kind"));
    CHECK(row.contains("amount_cents"));
    CHECK(row.at("amount_cents").is_number_integer());
    CHECK_FALSE(row.contains("time_quality"));

    // The overview is a table too; date/time columns stay Unix seconds.
    const auto transactions = to_json(s.ledger, ReportKind::Transactions, november);
    REQUIRE_FALSE(transactions.at("rows").empty());
    CHECK(transactions.at("rows").at(0).at("timestamp").is_number_integer());
}

TEST_CASE("all-time JSON omits unbounded from/to") {
    Scenario s;
    const auto payload = to_json(s.ledger, ReportKind::Transactions, all());
    CHECK(payload.at("kind") == "transactions");
    CHECK(payload.at("period") == "all");
    CHECK_FALSE(payload.contains("from"));
    CHECK_FALSE(payload.contains("to"));
    CHECK_FALSE(payload.at("rows").empty());

    const auto month_payload = to_json(s.ledger, ReportKind::Overview, month(NOV_02));
    CHECK(month_payload.contains("from"));
    CHECK(month_payload.contains("to"));
    CHECK(month_payload.at("to") == month(NOV_02).to);
}

TEST_CASE("golden CSV files for every report and format") {
    Scenario s;
    const Period november = month(NOV_02);
    const ReportKind kinds[] = {ReportKind::Overview,    ReportKind::Consumption,
                                ReportKind::Deposits,    ReportKind::Expenses,
                                ReportKind::Cashflow,    ReportKind::Balances,
                                ReportKind::Transactions};
    for (ReportKind kind : kinds) {
        const std::string base = report_kind_name(kind);
        check_golden(base + "_excel.csv", render(s.ledger, kind, november, CsvFormat::Excel));
        check_golden(base + "_raw.csv", render(s.ledger, kind, november, CsvFormat::Raw));
    }
}

TEST_CASE("long-term months group by Europe/Berlin and net voids to the original month") {
    Fixture f;
    const std::string ada = f.user("Ada", OCT_01);
    const std::string kaffee = f.product("Kaffee", 50, OCT_01);

    const Event october = f.must(f.ledger.purchase(ada, kaffee, "oct", OCT_15, TimeSource::Synced));
    f.must(f.ledger.guest_sale(kaffee, "guest-nov", NOV_02, TimeSource::Synced));
    // The void is recorded in December but must reduce October.
    f.must(f.ledger.void_event(october.id, "late", "void", DEC_02, TimeSource::Synced));

    const std::vector<LongTermMonth> months = long_term_months(f.ledger);
    REQUIRE(months.size() == 2);
    CHECK(months[0].year == 2026);
    CHECK(months[0].month == 10);
    CHECK(months[0].member_items == 0);
    CHECK(months[0].member_spend_cents == 0);
    CHECK(months[0].guest_items == 0);
    CHECK(months[1].year == 2026);
    CHECK(months[1].month == 11);
    CHECK(months[1].member_items == 0);
    CHECK(months[1].guest_items == 1);
    CHECK(months[1].guest_spend_cents == 50);
}
