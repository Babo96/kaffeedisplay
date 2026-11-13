import { readFileSync } from "node:fs";
import { expect, test, type APIRequestContext, type Page } from "@playwright/test";

const PASSWORD = "kaffee";
const MOCK = "http://127.0.0.1:8787/api/v1";

async function resetMock(request: APIRequestContext) {
  const response = await request.post(`${MOCK}/__test/reset`);
  expect(response.ok()).toBeTruthy();
}

async function login(page: Page) {
  await page.goto("/");
  await page.getByLabel("Passwort").fill(PASSWORD);
  await page.getByRole("button", { name: "Anmelden" }).click();
  await expect(page.getByRole("heading", { name: "Übersicht" })).toBeVisible();
}

test.beforeEach(async ({ request }) => {
  await resetMock(request);
});


test("login rejects a wrong password and accepts the admin", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "Admin-Anmeldung" })).toBeVisible();

  await page.getByLabel("Passwort").fill("falsch");
  await page.getByRole("button", { name: "Anmelden" }).click();
  await expect(page.getByRole("alert")).toContainText("Passwort falsch");

  await page.getByLabel("Passwort").fill(PASSWORD);
  await page.getByRole("button", { name: "Anmelden" }).click();
  await expect(page.getByRole("heading", { name: "Übersicht" })).toBeVisible();
  await expect(page.getByText("Kassenstand")).toBeVisible();

  await page.locator("aside button:visible", { hasText: "Abmelden" }).click();
  await expect(page.getByRole("heading", { name: "Admin-Anmeldung" })).toBeVisible();
});

test("creates a user", async ({ page }) => {
  await login(page);
  await page.goto("/users");
  await page.getByRole("button", { name: "Benutzer anlegen" }).click();

  const name = `Testuser ${Date.now()}`;
  const dialog = page.getByRole("dialog");
  await expect(dialog.getByRole("button", { name: "Zufällig vergeben (Standard)" })).toHaveAttribute("aria-pressed", "true");
  const post = page.waitForRequest((request) =>
    request.url().endsWith("/api/v1/users") && request.method() === "POST",
  );
  await dialog.getByLabel("Name").fill(name);
  await dialog.getByRole("button", { name: "Speichern" }).click();
  expect((await post).postDataJSON()).not.toHaveProperty("avatar_id");

  await expect(page.getByText("Benutzer angelegt.")).toBeVisible();
  const card = page.getByRole("listitem").filter({ hasText: name });
  await expect(card).toBeVisible();
  const firstCreated = (await (await page.request.get(`${MOCK}/users`)).json())
    .find((candidate: { name: string }) => candidate.name === name);
  expect(firstCreated.avatar_id).toBeGreaterThanOrEqual(1);
  expect(firstCreated.avatar_id).toBeLessThanOrEqual(30);

  await card.getByRole("button", { name: "Archivieren" }).click();
  await page.getByRole("dialog").getByRole("button", { name: "Archivieren" }).click();
  await expect(card).toHaveCount(0);
  await page.getByLabel("Inaktive anzeigen").check();
  await expect(card).toBeVisible();
  await card.getByRole("button", { name: "Wiederherstellen" }).click();
  await expect(page.getByText("Benutzer wiederhergestellt.")).toBeVisible();
});

test("admin can explicitly select initials-only or a preset for new users", async ({ page }) => {
  await login(page);
  await page.goto("/users");

  await page.getByRole("button", { name: "Benutzer anlegen" }).click();
  let dialog = page.getByRole("dialog");
  await dialog.getByLabel("Name").fill("Only Initials");
  await dialog.getByRole("button", { name: "Nur Initialen" }).click();
  await dialog.getByRole("button", { name: "Speichern" }).click();
  let result = await (await page.request.get(`${MOCK}/users`)).json();
  expect(result.find((u: { name: string }) => u.name === "Only Initials")?.avatar_id).toBe(0);

  await page.getByRole("button", { name: "Benutzer anlegen" }).click();
  dialog = page.getByRole("dialog");
  await dialog.getByLabel("Name").fill("Chosen Avatar");
  await dialog.getByRole("button", { name: "Motiv 17" }).click();
  await dialog.getByRole("button", { name: "Speichern" }).click();
  result = await (await page.request.get(`${MOCK}/users`)).json();
  expect(result.find((u: { name: string }) => u.name === "Chosen Avatar")?.avatar_id).toBe(17);
});

test("records a topup for a member", async ({ page }) => {
  await login(page);
  await page.goto("/users");

  const ada = page.getByRole("listitem").filter({ hasText: "Ada" }).first();
  await ada.getByRole("button", { name: "Einzahlung buchen" }).click();

  const dialog = page.getByRole("dialog");
  await dialog.getByPlaceholder("0,00").fill("5,00");
  await dialog.getByRole("button", { name: "Speichern" }).click();

  await expect(page.getByText("Einzahlung gebucht.")).toBeVisible();
  await expect(page.getByRole("listitem").filter({ hasText: "Ada" }).first()).toContainText("17,50 €");
});

test("downloads an Excel-compatible CSV report with a period filename", async ({ page }) => {
  await login(page);
  await page.goto("/reports");
  await expect(page.getByRole("heading", { name: "Berichte" })).toBeVisible();

  const downloadPromise = page.waitForEvent("download");
  await page.getByRole("button", { name: "CSV (Excel)" }).click();
  const download = await downloadPromise;

  expect(download.suggestedFilename()).toMatch(/^kaffeekasse_\d{4}-\d{2}_overview\.csv$/);
  const path = await download.path();
  expect(path).toBeTruthy();
  const content = readFileSync(path!, "utf8");
  expect(content).toContain("sep=;");
  expect(content).toContain("Kassenstand");

  await page.getByRole("button", { name: "Benutzerdefiniert" }).click();
  await page.getByLabel("Startdatum").fill("2026-03-01");
  await page.getByLabel("Enddatum").fill("2026-03-31");
  await expect(page.getByText("01.03.2026 – 31.03.2026")).toBeVisible();

  const customDownload = page.waitForEvent("download");
  await page.getByRole("button", { name: "CSV (Excel)" }).click();
  expect((await customDownload).suggestedFilename()).toBe(
    "kaffeekasse_2026-03_overview.csv",
  );
});

test("product CRUD sends a full ProductInput for reorder and archive", async ({ page }) => {
  const patches: Array<Record<string, unknown>> = [];
  page.on("request", (request) => {
    if (request.method() === "PATCH" && request.url().includes("/api/v1/products/")) {
      patches.push(request.postDataJSON() as Record<string, unknown>);
    }
  });

  await login(page);
  await page.goto("/products");
  const name = `Testgetränk ${Date.now()}`;

  await page.getByRole("button", { name: "Getränk anlegen" }).click();
  const create = page.getByRole("dialog");
  await create.getByLabel("Bezeichnung").fill(name);
  await create.getByLabel("Preis in Euro").fill("1,20");
  await create.getByRole("button", { name: "Speichern" }).click();
  await expect(page.getByText("Getränk angelegt.")).toBeVisible();

  const card = page.getByRole("listitem").filter({ hasText: name });
  await expect(card).toBeVisible();

  // Reorder: two full PATCH bodies.
  await page
    .getByRole("listitem")
    .filter({ hasText: "Espresso" })
    .getByRole("button", { name: "Nach oben" })
    .click();
  await expect
    .poll(() => patches.length)
    .toBeGreaterThanOrEqual(2);

  // Archive, restore and delete must all use the full body too.
  await card.getByRole("button", { name: "Archivieren" }).click();
  await page.getByRole("dialog").getByRole("button", { name: "Archivieren" }).click();
  await expect(card).toHaveCount(0);

  await page.getByLabel("Archivierte anzeigen").check();
  await expect(card).toBeVisible();
  await card.getByRole("button", { name: "Wiederherstellen" }).click();
  await expect(page.getByText("Getränk wiederhergestellt.")).toBeVisible();

  await card.getByRole("button", { name: "Löschen" }).click();
  await page.getByRole("dialog").getByRole("button", { name: "Endgültig löschen" }).click();
  await expect(page.getByText("Getränk gelöscht.")).toBeVisible();

  const required = ["name", "category", "price_cents", "icon", "color", "sort", "active"];
  for (const body of patches) {
    for (const key of required) expect(body).toHaveProperty(key);
  }
});

test("creates and voids an expense", async ({ page }) => {
  await login(page);
  await page.goto("/expenses");
  const description = `Testausgabe ${Date.now()}`;

  await page.getByRole("button", { name: "Ausgabe erfassen" }).click();
  const dialog = page.getByRole("dialog");
  await dialog.getByLabel("Beschreibung").fill(description);
  await dialog.getByLabel("Betrag in Euro").fill("12,34");
  await dialog.getByRole("button", { name: "Speichern" }).click();
  await expect(page.getByText("Ausgabe erfasst.")).toBeVisible();

  const row = page.getByRole("row").filter({ hasText: description });
  await expect(row).toBeVisible();
  await row.getByRole("button", { name: "Stornieren" }).click();

  const voidDialog = page.getByRole("dialog");
  await voidDialog.getByLabel("Grund").fill("Doppelt erfasst");
  await voidDialog.getByRole("button", { name: "Stornieren" }).click();
  await expect(page.getByText("Ausgabe storniert.")).toBeVisible();
  await expect(row).toContainText("Storniert");
});

test("applies display settings and starts/stops the AP with QR codes", async ({ page }) => {
  await login(page);
  await page.goto("/settings");
  // Rotated modes are not implemented on this panel (T-011): only 0° is offered.
  await expect(page.getByRole("button", { name: "0°", exact: true })).toBeEnabled();
  for (const angle of ["90°", "180°", "270°"] as const) {
    await expect(page.getByRole("button", { name: angle, exact: true })).toBeDisabled();
  }
  await expect(
    page.getByText("werden von diesem Display nicht unterstützt", { exact: false }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Speichern", exact: true }).click();
  await expect(page.getByText("Anzeige aktualisiert.")).toBeVisible();

  await page.goto("/system");
  await page.getByText("WLAN-Zugangspunkt").waitFor();
  await page.getByRole("button", { name: "WLAN starten" }).click();
  await expect(page.getByText("WLAN aktiv")).toBeVisible();
  await expect(
    page.getByRole("img", { name: "QR-Code: WLAN beitreten" }),
  ).toBeVisible();
  await expect(
    page.getByRole("img", { name: "QR-Code: Admin-Seite öffnen" }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Werkseinstellungen" }),
  ).toBeEnabled();

  await page.getByRole("button", { name: "WLAN stoppen" }).click();
  const stop = page.getByRole("dialog");
  await expect(stop).toContainText("WLAN wirklich stoppen");
  await stop.getByRole("button", { name: "WLAN stoppen" }).click();
  await expect(page.getByText("WLAN aus")).toBeVisible();
});

test("shows the untrusted-time alert after login", async ({ page, request }) => {
  await login(page);
  const response = await request.post(`${MOCK}/__test/time-untrusted`);
  expect(response.ok()).toBeTruthy();
  await page.reload();
  await expect(
    page.getByText("Die Gerätezeit ist nicht synchronisiert.", { exact: false }),
  ).toBeVisible();
});

test("renders the dashboard at 360 px", async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 780 });
  await login(page);
  await expect(page.getByText("Kassenstand")).toBeVisible();
  const hasHorizontalOverflow = await page.evaluate(
    () => document.documentElement.scrollWidth > document.documentElement.clientWidth + 1,
  );
  expect(hasHorizontalOverflow).toBe(false);
});

test("factory reset requires the typed confirmation and calls the API", async ({ page }) => {
  await login(page);
  await page.goto("/system");
  await page.getByRole("button", { name: "Werkseinstellungen" }).click();
  const dialog = page.getByRole("dialog");
  await expect(dialog).toContainText("Werkseinstellungen wiederherstellen?");
  const confirm = dialog.getByRole("button", { name: "Werkseinstellungen" });
  await expect(confirm).toBeDisabled();
  await dialog.getByRole("textbox").fill("WERKSRESET");
  await expect(confirm).toBeEnabled();
  await confirm.click();
  await expect(page.getByText("Werkseinstellungen werden wiederhergestellt.")).toBeVisible();
});
