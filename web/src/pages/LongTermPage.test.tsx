import { afterEach, describe, expect, it, vi } from "vitest";
import { render, screen, waitFor } from "@testing-library/react";
import { LongTermPage } from "./LongTermPage";
import { de } from "../i18n/de";
import type { LongTermStats } from "../api/types";

const stats: LongTermStats = {
  first_sale_at: 1_760_000_000,
  last_sale_at: 1_763_000_000,
  member_items: 3,
  guest_items: 1,
  member_spend_cents: 150,
  guest_spend_cents: 30,
  estimated_items: 0,
  users: [
    { user_id: "usr_ada", name: "Ada", color: "#D4A373", items: 2, spend_cents: 100 },
    { user_id: "usr_bob", name: "Bob", color: "#8FBC8F", items: 1, spend_cents: 50 },
  ],
  products: [
    { product_id: "prd_kaffee", name: "Kaffee", category: "Kaffee", items: 3, spend_cents: 150 },
  ],
  months: [
    {
      year: 2026,
      month: 11,
      member_items: 3,
      guest_items: 1,
      member_spend_cents: 150,
      guest_spend_cents: 30,
    },
  ],
  time_quality: { trusted: true, source: "synced" },
};

function stubFetch(body: LongTermStats) {
  vi.stubGlobal(
    "fetch",
    vi.fn(async () => {
      return new Response(JSON.stringify(body), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      });
    }),
  );
}

describe("LongTermPage", () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("renders per-user, per-product and per-month statistics", async () => {
    stubFetch(stats);
    render(<LongTermPage />);

    expect(screen.getByText(de.longTermTitle)).toBeInTheDocument();
    await waitFor(() => expect(screen.getByText("Ada")).toBeInTheDocument());
    expect(screen.getByText("Bob")).toBeInTheDocument();
    expect(screen.getByText("Kaffee")).toBeInTheDocument();
    expect(screen.getByText(de.longTermPerMonth)).toBeInTheDocument();
    expect(screen.getByText(de.trustedTime)).toBeInTheDocument();
  });

  it("shows an honest empty state without sales", async () => {
    stubFetch({
      ...stats,
      member_items: 0,
      guest_items: 0,
      member_spend_cents: 0,
      guest_spend_cents: 0,
      first_sale_at: 0,
      last_sale_at: 0,
      users: [],
      products: [],
      months: [],
    });
    render(<LongTermPage />);

    await waitFor(() => expect(screen.getByText(de.longTermEmpty)).toBeInTheDocument());
    expect(screen.queryByText(de.longTermPerUser)).not.toBeInTheDocument();
  });
});
