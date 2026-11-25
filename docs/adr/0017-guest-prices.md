# ADR 0017: Optional guest prices

Status: Accepted

## Context

Products have exactly one price. Guests pay cash at the kiosk, and the fund
wants to charge them a different price than the members' tab — for example a
higher price that covers the missing prepaid float, or a lower introductory
price. The reporter asked for an admin-configurable guest price that the guest
sees when tapping a drink.

Three constraints shape the design:

1. `docs/api/openapi.yaml`, `domain.hpp` and `service.hpp` are frozen contracts;
   they change together with every consumer in one branch.
2. Journal events are hash-chained and never mutated. Adding a persisted field
   must not change the canonical JSON of events written by older firmware, or
   replay would fail the chain check.
3. Balances and revenue are derived from `amount_cents`. Guest revenue must not
   drift when an admin edits a price later, exactly like member purchases.

## Decision

- **One optional `guest_price_cents` on `Product` and on `Event`.** The sentinel
  `kNoGuestPrice = -1` means "no separate guest price; guests pay
  `price_cents`". `-1` cannot collide with a real price because prices are
  validated as `>= 0` (`0` is a legitimate free drink). This mirrors the
  existing `avatar_id = -1` convention in the user API and avoids a second
  boolean that could disagree with the amount.
- **Range validation lives in the domain and the REST layer.** `-1` or
  `0..kMaxPriceCents` (100000 = 1000.00 EUR) is accepted; anything else is
  rejected before the commit hook runs, so invalid values never reach the
  journal. The admin SPA validates the same range.
- **The guest sale snapshots the price actually charged.** `guest_sale` writes
  the effective price (guest price when set, member price otherwise) to
  `unit_amount_cents` and `amount_cents`, and stores the product's guest price
  in `guest_price_cents` (`-1` when it inherited). Editing a price afterwards
  therefore cannot change historical guest revenue; voids copy the original
  snapshot.
- **Member purchases ignore the guest price.** `purchase` continues to snapshot
  `price_cents` and records `guest_price_cents = -1`.
- **Event JSON omits `guest_price_cents` when it is `-1`.** Old events
  deserialize to `-1` and serialize without the key, so their canonical hash and
  the chain are unchanged. `0` is written because it is a real price.
- **Product JSON uses `null` on the wire** (`guest_price_cents: null`) instead
  of exposing the sentinel. Missing keys in old product JSON and backups read
  as `null`.
- **PATCH semantics:** absent keeps the current value, `null` clears it, a
  number sets it. POST treats absent/`null` as "inherit".
- **Device UI shows what the guest pays.** In guest mode the product tile renders
  the guest price (or the member price when unset) and the sale toast names the
  "Gastpreis". The booking flow and the trust model are unchanged.
- **Reports keep using event amounts.** No report computes revenue from the
  current product table, so old guest sales without a guest price need no
  special case: their recorded amount is the member price they were charged.

## Consequences

- Existing journals, snapshots and backups replay unchanged; a host test replays
  an old-format product and guest sale.
- Admin product create/update carries one more nullable field; the SPA shows both
  prices and a hint that an empty guest price means "same as members".
- Guest revenue stays truthful over price edits and voids.
- Rolling back to firmware without this feature ignores the extra event field.
  Its parser skips unknown keys, and the events still validate because
  `amount_cents` is unchanged. Product records written by the newer firmware
  lose the guest price on the next edit by the older firmware — acceptable for
  a downgrade, and documented here.

## Alternatives considered

- **A separate `has_guest_price` flag plus a cents value.** Two fields can
  disagree and the event hash would include both. The sentinel is smaller.
- **Charging guests `price_cents` and applying a percentage surcharge.** Adds
  rounding rules and makes receipts harder to explain.
- **Recomputing guest revenue from the current product prices in reports.**
  Rewrites history on every price change; rejected by the ledger invariants.
