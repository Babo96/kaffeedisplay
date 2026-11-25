#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kaffee/domain.hpp"
#include "kaffee/json.hpp"

// Report generation and streaming CSV export.
//
// The component is pure C++17 and host-testable: it only reads a `Ledger` and
// writes through a caller supplied sink. No report is buffered as a whole file.
//
// Period boundaries use Europe/Berlin wall-clock time and the EU DST rule
// (last Sunday of March 01:00 UTC until last Sunday of October 01:00 UTC).
// `Period::from` is inclusive, `Period::to` is exclusive.
//
// Attribution policy:
// - `Overview` booking aggregates and `Consumption` use the original booking
//   period: a void nets out the original booking (and increments `Stornos`),
//   matching `Ledger::leaderboard()`. A late void therefore changes the report
//   of the original period, not the month in which it was reversed.
// - `Deposits`, `Cashflow` and `Transactions` are timestamp based: a void is a
//   cash movement (or an audit row) in the period in which it was recorded.
// - `Balances` reconstruct user name/status and balances as of `Period::to`.
// - `Expenses` is an event-delta log: created = -amount, updated = -(new-old),
//   voided = +amount. It is not a snapshot of current expense values.
// - Time quality is exported per row in `Transactions`/`Expenses` and as the
//   "Geschätzte Zeitangaben" count in `Overview`.
namespace kaffee::reports {

enum class PeriodKind { Week, Month, Year, All, Custom };

enum class ReportKind {
    Overview,
    Consumption,
    Deposits,
    Expenses,
    Cashflow,
    Balances,
    Transactions,
};

enum class CsvFormat {
    Excel,  // UTF-8 BOM, sep=;, semicolon, CRLF, German decimals/dates
    Raw,    // comma, dot, ISO 8601
};

struct Period {
    Timestamp from = 0;
    Timestamp to = 0;  // exclusive
    PeriodKind kind = PeriodKind::Custom;
};

// ISO week Monday 00:00 Europe/Berlin.
Period week(Timestamp now);
// Calendar month in Europe/Berlin.
Period month(Timestamp now);
// Calendar year in Europe/Berlin.
Period year(Timestamp now);
// Everything; `from` is 0 and `to` is the largest timestamp.
Period all();
Period custom(Timestamp from, Timestamp to);
// Dispatch for the four standard kinds. `Custom` returns an empty (invalid)
// period because it needs explicit bounds.
Period period(PeriodKind kind, Timestamp now);

bool contains(const Period& period, Timestamp ts);
bool valid(const Period& period);

// Civil time in Europe/Berlin. `weekday` is ISO 1=Monday ... 7=Sunday.
struct Civil {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int weekday = 4;
};

Civil to_berlin(Timestamp utc);
// Converts a Berlin wall-clock value to UTC. Ambiguous/inexistent values only
// occur around 02:00-03:00, never at a period boundary.
Timestamp from_berlin(const Civil& local);
bool berlin_is_dst(Timestamp utc);
// 3600 (CET) or 7200 (CEST).
int berlin_utc_offset(Timestamp utc);

enum class ColumnType {
    Text,
    MoneyEur,    // cents rendered as EUR with two decimals
    MoneyCents,  // cents rendered as an integer
    Date,        // date only
    DateTime,    // date and time
    Integer,
    Bool,
};

struct Column {
    std::string_view key;    // stable raw header, e.g. "amount_cents"
    std::string_view label;  // German Excel header
    ColumnType type = ColumnType::Text;
};

struct Value {
    std::string text;
    Cents cents = 0;
    Timestamp ts = 0;
    int64_t number = 0;
    bool flag = false;

    static Value of_text(std::string value);
    static Value money(Cents value);
    static Value date(Timestamp value);
    static Value datetime(Timestamp value);
    static Value integer(int64_t value);
    static Value boolean(bool value);
};

using RowSink = std::function<bool(const std::vector<Value>&)>;
using WriteFn = std::function<bool(std::string_view)>;

struct Sink {
    WriteFn write;
};

// Columns for a report. The same table is used by JSON and CSV consumers;
// only the header labels differ (`key` for raw, `label` for Excel).
const std::vector<Column>& columns(ReportKind kind);

// Emits the header-less rows one at a time. The sink returns false to abort.
// No report accumulates the full result set; transaction and expense rows are
// produced directly from the ledger event stream.
Result<Unit> for_each_row(const Ledger& ledger, ReportKind kind, const Period& period,
                          const RowSink& sink);

// Streams a complete CSV document (BOM/delimiter line, header, rows) to the
// sink. The sink is never handed the whole file at once.
Result<Unit> write_csv(const Ledger& ledger, ReportKind kind, const Period& period,
                       CsvFormat format, const Sink& sink);

// Builds the JSON envelope described in docs/api/reports.md:
// `kind`, `period`, `from`, `to`, `columns` and `rows`. Money cells are
// integer cents; date/datetime cells are Unix seconds. The caller adds
// `time_quality` because the report component has no clock/platform.
nlohmann::json to_json(const Ledger& ledger, ReportKind kind, const Period& period);

// Low-level writer, reusable by the API for other tables.
class CsvWriter {
  public:
    CsvWriter(CsvFormat format, Sink sink);

    // UTF-8 BOM and the `sep=;` hint for Excel; nothing for raw.
    bool begin();
    bool header(const std::vector<Column>& columns);
    bool row(const std::vector<Column>& columns, const std::vector<Value>& values);
    bool end();

    bool ok() const {
        return ok_;
    }

  private:
    bool emit(std::string_view chunk);

    CsvFormat format_;
    Sink sink_;
    bool ok_ = true;
    bool began_ = false;
};

std::string format_money(Cents cents, CsvFormat format);
std::string format_date(Timestamp ts, CsvFormat format);
std::string format_datetime(Timestamp ts, CsvFormat format);
// RFC 4180 quoting for the format's delimiter.
std::string escape_field(std::string_view value, CsvFormat format);
// Prefixes cells that would start a spreadsheet formula, unless the text is a
// valid number. Numeric column types are never touched.
std::string protect_formula(std::string_view value);

std::string report_kind_name(ReportKind kind);
std::optional<ReportKind> report_kind_from_name(std::string_view name);
std::string period_kind_name(PeriodKind kind);
std::optional<PeriodKind> period_kind_from_name(std::string_view name);

const char* csv_content_type(CsvFormat format);
// Distinct, non-colliding names per period kind:
// `kaffeekasse_YYYY-MM_<report>.csv` (month),
// `kaffeekasse_YYYY-Www_<report>.csv` (ISO week, e.g. `2026-W53`),
// `kaffeekasse_YYYY_<report>.csv` (year), `kaffeekasse_all_<report>.csv`,
// `kaffeekasse_YYYY-MM-DD_YYYY-MM-DD_<report>.csv` (custom), and
// `kaffeekasse_YYYY-MM-DD_offen_<report>.csv` when a custom end is open.
std::string report_filename(ReportKind kind, const Period& period);

// Monthly counts net of voids; delegates to Ledger::leaderboard so a late void
// is attributed to the original booking's period.
Leaderboard leaderboard(const Ledger& ledger, const Period& period);
Leaderboard monthly_leaderboard(const Ledger& ledger, Timestamp now);

// All-time consumption grouped by Europe/Berlin calendar month. Membership and
// void attribution match `Ledger::long_term_stats()`: a void decrements the
// month of the original booking, even when the reversal was recorded later.
struct LongTermMonth {
    int year = 1970;
    int month = 1;
    int member_items = 0;
    int guest_items = 0;
    Cents member_spend_cents = 0;
    Cents guest_spend_cents = 0;
};
std::vector<LongTermMonth> long_term_months(const Ledger& ledger);

}  // namespace kaffee::reports
