# ADR 0014: Built-in user avatar presets

Status: Accepted

## Context

The booking UI identifies members using coloured initials. The administrator wants a lightweight, optional, more distinctive visual identifier without an image uploader, filesystem assets, runtime downloads, or substantial memory use. User details cross the frozen domain, service, and REST interfaces.

## Decision

- Add an integer `avatar_id` to the user record and admin REST API: `0` (initials-only or legacy user), or `1..30` (preset). For new users an omitted field requests a random preset; an explicit zero requests initials-only.
- Persist avatar selection in user-created/updated/archived ledger events as `user_avatar_id`. Omit the field when zero to preserve legacy event hashes. Missing values default to zero when replaying.
- Assign omitted avatar IDs on POST server-side using the platform random number generator; exclude the previously created user's preset to avoid immediate duplicates. Choose the value **after idempotency lookup** so retries do not reroll. The selected ID is persisted once in the user-created event. Explicit presets and initials-only remain admin-selectable.
- Preserve existing avatar selection on PATCH when `avatar_id` is absent or null, including archive/restore. Explicit zero resets to initials-only. On create, absent or null means "let the server choose".
- Ship 30 **distinct static cartoon illustrations**, indexed 1–30 (animals, technology, coffee and objects). A checked-in compact source pack is deterministically expanded into **38 × 38, LVGL I4 indexed image resources**, with 16-color ARGB8888 palettes, and matching embedded PNG images for the admin UI. The device overlays a small initials badge; the compact web picker displays unobscured motifs. LVGL's built-in indexed-image decoder is used at draw time (ARGB8888 support required); no custom decoder, uploads, network or extra animation timers are required.
- Authorize avatar changes only through the existing authenticated administrator user-management API.
- Do not affect product selection, money, accounting, or backup schema versions.

## Consequences

The domain/service/OpenAPI contracts and all consumers are changed in one branch. User data and journal events remain self-contained and backup-compatible. The artwork now adds 30 × (16 × 4 + 38 × 19) = **23,580 bytes of I4 palette and index data in firmware flash**, down from **86,640 bytes** with RGB565: a theoretical 63,060-byte reduction before differences in rendering code or linking. Indexed images use LVGL's built-in software decoder and may temporarily allocate expanded pixel data; flash usage falls under the generic image-size budget in `make check`, and memory overhead was measured on the device (`status` reports internal heap and LVGL stack watermarks) rather than asserted in CI. Image objects are only created for users on the active screen. The simulator and CI verify compatibility, visual appearance, and build size.

## Alternatives considered

Uploading pictures would require image validation, file storage, image decoding and backup handling. Shipping bitmaps could be compact, but also entails binary assets and an image cache. Both add unnecessary complexity for 30 fixed designs.
