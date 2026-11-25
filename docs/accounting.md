# Accounting

The Kaffeekasse is a **post-pay tab** for members plus **cash sales for guests**.

## Semantics

- Members consume now and settle later. A member balance may be positive
  (credit) or negative (open amount).
- Admin records a payment (`topup`) when cash is handed over. That increases the
  member balance / reduces debt and increases the physical cash expectation.
- A member purchase debits the member's tab but does **not** change cash.
- A guest sale is paid cash immediately: it increases expected cash and guest
  revenue, and never touches a member balance. The amount charged is the
  product's `guest_price_cents` when set, otherwise its `price_cents`; the event
  snapshots the amount actually paid.
- Expenses decrease expected cash. They never touch member balances.
- Corrections are `void` (reversal) events, never deletes or edits.
- An `adjustment` changes a member balance without changing cash and requires a
  note. Use it for write-offs or goodwill, not for cash payments.

## Formulas

```
balance(user)  = Σ events.amount_cents for user
cash_expected  = topups + guest_sales − expenses
                 (voids are included in each sum with the opposite sign)
prepaid        = Σ max(balance, 0)
debts          = Σ max(−balance, 0)
net_liabilities = prepaid − debts
equity         = cash_expected − net_liabilities
```

## Cash reconciliation (Kassensturz)

Admin enters the physically counted cash. The device stores a `CashCount` with
`counted_cents`, `expected_cents` and the difference. It does **not** alter
balances. If the difference should be booked, the admin records an expense
(category `Kassendifferenz`) or an adjustment with a note.

## Reports

- Consumption: member items/revenue and guest items/revenue, per user/product.
- Deposits: cash payments per member.
- Expenses: event-delta log by category/vendor/date; created rows are negative,
  updates carry `−(new−old)`, voids are the positive reversal. This preserves
  the audit trail; it is not a snapshot of current expense values.
- Cash flow: opening cash, deposits, guest sales, expenses, closing cash.
- Balances: balance per member as of the period end, prepaid/debt split, with
  the user name and active/archived status reconstructed as of that instant.
- Transactions: line-level ledger with `payment` (`Guthaben` / `Bar (Gast)`) and
  void markers.
- Periods: ISO week (Mo–So), calendar month, calendar year, all time, custom.
  Boundaries use `Europe/Berlin`.
- Time quality is included (`Zeitqualität`); estimated timestamps are flagged
  per row in transactions/expenses and counted in the overview.

### Void attribution

Voids are attributed to the original booking period for consumption, the
overview booking aggregates and the leaderboard. A late void therefore makes the
original month net to zero (the row stays visible through its `Stornos` count)
instead of creating negative consumption in the reversal month.

Cash flow, deposits and the line-level transaction log behave differently: they
are timestamp based, so the money movement is reported in the month in which the
void was recorded. Use cash flow for the physical cash history and consumption
for "what was consumed in this period now that all corrections are known".

## Excel-compatible CSV

Default `csv_excel`:

- UTF-8 BOM, `sep=;`, semicolon delimiter, CRLF.
- German dates `TT.MM.JJJJ`, decimal comma, no thousands separators.
- German headers; amounts in EUR and cents columns.
- Escape quotes; prefix formula-leading cells (`= + - @`) unless they are valid
  numbers.
- Stream rows; do not build the whole file in RAM.
- Filenames never collide between period kinds:
  `kaffeekasse_YYYY-MM_<report>.csv` (month),
  `kaffeekasse_YYYY-Www_<report>.csv` (ISO week),
  `kaffeekasse_YYYY_<report>.csv` (year),
  `kaffeekasse_all_<report>.csv`, and
  `kaffeekasse_YYYY-MM-DD_YYYY-MM-DD_<report>.csv` (custom). A custom period
  with an open end uses `YYYY-MM-DD_offen`.

`csv_raw`: comma delimiter, dot decimals, ISO timestamps for scripts.

## Report component

`firmware/components/reports` implements the report tables and CSV streaming.
It is pure C++17, depends only on `domain` and never buffers a complete file.

```cpp
#include "kaffee/reports.hpp"

using namespace kaffee::reports;
Period period = month(now);
Sink sink{[&response](std::string_view chunk) { return response.write(chunk); }};
auto result = write_csv(ledger, ReportKind::Consumption, period, CsvFormat::Excel, sink);
```

`for_each_row()` yields the same rows to a JSON renderer. Periods carry explicit
`from`/`to` Unix seconds with an exclusive end; `period(kind, now)` computes the
Europe/Berlin week/month/year variants. `csv_raw` uses stable English header
keys, comma, dot decimals and ISO 8601 timestamps with offset for scripts.
Formula protection is applied to Excel text cells only; negative numeric cells
are never prefixed. `report_filename()` emits distinct month (`YYYY-MM`), ISO
week (`YYYY-Www`), year (`YYYY`), all (`all`) and custom date-range names.

## Example

| Action | Member balance | Cash expected | Equity |
|---|---:|---:|---:|
| Start | 0 | 0 | 0 |
| Ada pays 10 € | +10.00 | +10.00 | 0 |
| Ada buys 0.50 € | +9.50 | +10.00 | +0.50 |
| Guest buys 0.80 € cash | +9.50 | +10.80 | +1.30 |
| Machine service 8 € | +9.50 | +2.80 | −6.70 |

Equity is the fund's net position, not a per-member number. A negative equity
means the fund has spent more than it has collected/consumed so far.
