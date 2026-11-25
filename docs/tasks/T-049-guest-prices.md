# T-049 — Optional guest prices

Status: in-progress
Track: domain/api/ui/web
Issue: [#49](https://github.com/Babo96/kaffeedisplay/issues/49)
Branch: `feat/49-guest-prices` (from `main` 2633f24)
ADR: [0017](../adr/0017-guest-prices.md)

## Scope

- Admins can set a separate guest price per product in the web UI; an empty
  field means "guests pay the member price".
- Circle guest sales charge the guest price and snapshot it in the event;
  member purchases keep the member price.
- The device shows the guest price on the drink tile in guest mode and in the
  sale toast after a tap.
- Domain, service, OpenAPI, mock, simulator seed and docs stay in sync.

## Acceptance

- Products persist `guest_price_cents`; `-1`/`null` inherits, `0` is a free
  guest price, values outside `0..100000` are rejected.
- `POST/PATCH/GET /products` round-trip the field; PATCH absent keeps, `null`
  clears.
- Old journals, snapshots and backups replay with the same hashes; guest sales
  recorded before this change keep their amount.
- Editing a product price never changes historical guest revenue.
- Web, host, simulator and firmware builds pass; German labels only.

## Out of scope

- Member price changes, discounts, per-user prices or percentage surcharges.
- Showing guest prices outside the booking flow.
