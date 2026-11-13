# Data model

Money is `int64_t` cents. Timestamps are Unix seconds UTC. The UI renders in
`Europe/Berlin`.

## Entities

### User (coffee member, not an auth account)

```
id, name, color, avatar_id, active, created_at, archived_at
```

`avatar_id` is 0 for initials-only, or 1–30 for fixed firmware presets. New users automatically receive a random 1–30 preset when `avatar_id` is omitted (preferring presets no active user has); an explicit 0 keeps initials-only. The assigned ID is stored in user change events and included in backups. Missing legacy values default to 0.

Balances are derived from events. Archived users keep their history and debt.

### Product (drink / offering)

```
id, name, category, icon, color, price_cents, sort, active, created_at, updated_at
```

`icon` is a key from the built-in icon set. Deleting a product removes it from
the device; every historical event carries `product_name`, `product_category`
and `unit_amount_cents` snapshots.

### Admin

Single account, stored in NVS: `username=admin`, PBKDF2 hash + salt +
iterations. Never in the journal or backups.

### Event (journal record)

```
seq, id, ts, time_source, type,
user_id?, user_name?, user_color?, user_active?,
product_id?, product_name?, product_category?, product_icon?,
product_color?, product_sort?, product_active?, unit_amount_cents?,
amount_cents, payment?, note?, actor, request_id?, reverses?,
expense_id?, expense_category?, expense_description?, expense_vendor?,
counted_cents?, expected_cents?, meta?, prev_hash, hash
```

`amount_cents` is the authoritative financial delta:

| Type | amount_cents | Cash effect | Balance effect |
|---|---|---|---|
| `purchase` | `−price` | none | user −price |
| `guest_sale` | `+price` | +price | none |
| `topup` | `+amount` | +amount | user +amount |
| `adjustment` | ±amount | none | user ±amount |
| `void` | opposite of original | follows original | follows original |
| `expense_created` | `−amount` | −amount | none |
| `expense_updated` | `−(new−old)` | −(new−old) | none |
| `expense_voided` | `+amount` | +amount | none |
| `cash_count` | 0 | none | none |
| user/product events | 0 | none | none |
| `audit` / `settings_updated` | 0 | none | none |

`unit_amount_cents` is the unit price for purchases / product snapshots and the
new amount for `expense_updated`.

### Expense

```
id, ts, category, description, vendor, amount_cents, note, voided
```

### CashCount

```
id, ts, counted_cents, expected_cents, difference_cents, note
```

## Event types

`user_created`, `user_updated`, `user_archived`, `product_created`,
`product_updated`, `product_archived`, `product_deleted`, `purchase`,
`guest_sale`, `topup`, `adjustment`, `void`, `expense_created`,
`expense_updated`, `expense_voided`, `cash_count`, `settings_updated`, `audit`.

## Invariants

- `balance(user) = Σ event.amount_cents` for that user.
- `cash_expected = Σ topups + Σ guest_sales − Σ expenses` (voids included).
- `prepaid = Σ max(balance, 0)`, `debts = Σ max(−balance, 0)`.
- `net_liabilities = prepaid − debts`, `equity = cash_expected − net_liabilities`.
- A void is a new event; historical events are never mutated or deleted.
- At most one void per event.
- Product deletion does not alter history.
- `request_id` is unique; a retry returns the original event.
- Event hash = SHA-256 over canonical JSON without `hash`, including `prev_hash`.

## Journal and snapshots

- `journal.jsonl`: one event per line, JSON UTF-8.
- `snapshot.a.json` / `snapshot.b.json`: `{version, last_seq, head_hash, events[]}`.
- Boot: newest valid snapshot → replay journal where `seq > last_seq` → verify
  chain and state.
- Corrupt tail: stop at the first invalid event, copy the tail to `.corrupt`
  best-effort and truncate `journal.jsonl` in place (`ftruncate` + fsync). A
  failed copy or a full filesystem must not fail boot; append attempts are
  refused until the next successful load if truncation fails.
- Compaction: write the inactive snapshot, read it back and replay-validate the
  hash chain, then truncate the journal in place. The service schedules
  compaction after 256 journal events or when the partition is 90% full.
- Host tests use the same layout in a plain directory (`FileEventStore`). The
  device uses LittleFS (`LittleFsEventStore`) with the journal core shared.
- Journal appends are `fflush` + `fsync`; snapshot and journal replacement use a
  temporary file, `fsync`, rename and directory `fsync`. A failed append is
  rolled back to the pre-write offset; if rollback fails the store marks itself
  degraded and refuses further appends.

## Backup JSON

Versioned export of users, products, events, expenses, cash counts and
non-secret settings. No passwords, AP credentials or STA credentials.
