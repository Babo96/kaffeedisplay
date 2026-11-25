import { describe, expect, it } from "vitest";
import type { Product } from "../api/types";
import { toProductInput } from "./productInput";

function makeProduct(guestPrice: number | null): Product {
  return {
    id: "prd_kaffee",
    name: "Kaffee",
    category: "Kaffee",
    icon: "cup",
    color: "#D4A373",
    price_cents: 50,
    guest_price_cents: guestPrice,
    sort: 1,
    active: true,
    created_at: 0,
    updated_at: 0,
  };
}

describe("toProductInput", () => {
  it("keeps a configured guest price on partial updates", () => {
    const input = toProductInput(makeProduct(80), { active: false });
    expect(input.price_cents).toBe(50);
    expect(input.guest_price_cents).toBe(80);
    expect(input.active).toBe(false);
  });

  it("sends null so guests inherit the member price", () => {
    const input = toProductInput(makeProduct(null));
    expect(input.guest_price_cents).toBeNull();
  });

  it("allows clearing a guest price through an override", () => {
    const input = toProductInput(makeProduct(80), { guest_price_cents: null });
    expect(input.guest_price_cents).toBeNull();
  });
});
