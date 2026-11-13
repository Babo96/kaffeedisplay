import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { SettingsPage } from "./SettingsPage";
import { de } from "../i18n/de";
import type { Settings } from "../api/types";

function makeSettings(userSort: Settings["app"]["user_sort"]): Settings {
  return {
    display: {
      rotation: 0,
      brightness_active: 100,
      brightness_idle: 20,
      dim_timeout_s: 60,
      screen_off_timeout_s: 600,
    },
    network: {
      ap_ssid: "Kaffeekasse-B834",
      ap_window_s: 900,
      hostname: "kaffeekasse",
      sta_enabled: true,
      sta_ssid: "Werkstatt",
      napt_enabled: false,
    },
    time: { timezone: "Europe/Berlin", sntp_enabled: true, sntp_server: "pool.ntp.org" },
    app: {
      currency: "EUR",
      locale: "de-DE",
      guest_enabled: true,
      user_sort: userSort,
      max_open_debt_cents: 0,
      low_balance_warning_cents: -1000,
    },
  };
}

interface Call {
  url: string;
  method: string;
  body: unknown;
}

function stubFetch(settings: Settings) {
  const calls: Call[] = [];
  vi.stubGlobal(
    "fetch",
    vi.fn(async (url: string, init?: RequestInit) => {
      const method = init?.method ?? "GET";
      calls.push({
        url,
        method,
        body: typeof init?.body === "string" ? JSON.parse(init.body) : init?.body,
      });
      return new Response(JSON.stringify(settings), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      });
    }),
  );
  return calls;
}

describe("SettingsPage user order", () => {
  beforeEach(() => {
    vi.unstubAllGlobals();
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("offers the German choice and saves `usage` with the settings form", async () => {
    const calls = stubFetch(makeSettings("creation"));
    render(<SettingsPage />);

    const usage = await screen.findByRole("button", { name: de.userSortUsage });
    expect(screen.getByText(de.userSortLabel)).toBeInTheDocument();
    expect(screen.getByText(de.userSortHint)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: de.userSortCreation })).toHaveAttribute(
      "aria-pressed",
      "true",
    );

    fireEvent.click(usage);
    expect(usage).toHaveAttribute("aria-pressed", "true");

    fireEvent.click(screen.getByRole("button", { name: de.saveSettings }));

    await waitFor(() => {
      const put = calls.find((call) => call.method === "PUT");
      expect(put).toBeTruthy();
      expect((put?.body as Settings).app.user_sort).toBe("usage");
    });
  });

  it("shows the saved order from the device", async () => {
    stubFetch(makeSettings("usage"));
    render(<SettingsPage />);

    const usage = await screen.findByRole("button", { name: de.userSortUsage });
    expect(usage).toHaveAttribute("aria-pressed", "true");
    expect(screen.getByRole("button", { name: de.userSortCreation })).toHaveAttribute(
      "aria-pressed",
      "false",
    );
  });
});
