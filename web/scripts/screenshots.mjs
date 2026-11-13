import { spawn } from "node:child_process";
import { mkdir } from "node:fs/promises";
import path from "node:path";
import { chromium } from "@playwright/test";

const BASE = "http://127.0.0.1:5173";
const MOCK = "http://127.0.0.1:8787/api/v1/health";
const OUT = path.resolve(process.cwd(), "../docs/ui/screenshots/web");

const children = [];

async function isUp(url) {
  try {
    const response = await fetch(url, { signal: AbortSignal.timeout(1000) });
    return response.ok;
  } catch {
    return false;
  }
}

async function waitFor(url, timeoutMs = 30000) {
  const started = Date.now();
  while (Date.now() - started < timeoutMs) {
    if (await isUp(url)) return;
    await new Promise((resolve) => setTimeout(resolve, 300));
  }
  throw new Error(`Timeout waiting for ${url}`);
}

function start(command, args) {
  const child = spawn(command, args, {
    cwd: process.cwd(),
    stdio: "ignore",
    env: process.env,
    detached: true,
  });
  children.push(child);
  return child;
}

function stopChildren() {
  for (const child of children) {
    try {
      process.kill(-child.pid, "SIGTERM");
    } catch {
      child.kill("SIGTERM");
    }
  }
}

async function login(page) {
  await page.goto(`${BASE}/`);
  await page.getByLabel("Passwort").fill("kaffee");
  await page.getByRole("button", { name: "Anmelden" }).click();
  await page.getByRole("heading", { name: "Übersicht" }).waitFor();
}

async function shot(page, name, fullPage = true) {
  await page.waitForTimeout(400);
  await page.screenshot({ path: path.join(OUT, name), fullPage });
  console.log(`wrote ${name}`);
}

async function main() {
  await mkdir(OUT, { recursive: true });

  if (!(await isUp(MOCK))) start("node", ["mock/server.mjs"]);
  if (!(await isUp(BASE))) start("npx", ["vite", "--host", "127.0.0.1", "--port", "5173"]);
  await waitFor(MOCK);
  await waitFor(BASE);

  const browser = await chromium.launch();
  try {
    const context = await browser.newContext({
      viewport: { width: 1280, height: 900 },
      colorScheme: "dark",
    });
    const page = await context.newPage();
    await page.goto(`${BASE}/`);
    await page.getByRole("heading", { name: "Admin-Anmeldung" }).waitFor();
    await shot(page, "web-login.png", false);

    await login(page);
    await shot(page, "web-dashboard.png");

    const routes = [
      ["/users", "web-users.png", "Benutzer"],
      ["/products", "web-products.png", "Getränke"],
      ["/expenses", "web-expenses.png", "Ausgaben"],
      ["/cash", "web-cash.png", "Kassensturz"],
      ["/reports", "web-reports.png", "Berichte"],
      ["/settings", "web-settings.png", "Einstellungen"],
    ];
    for (const [route, file, heading] of routes) {
      await page.goto(`${BASE}${route}`);
      await page.getByRole("heading", { name: heading }).waitFor();
      await shot(page, file);
    }

    await page.goto(`${BASE}/system`);
    await page.getByRole("heading", { name: "System" }).waitFor();
    await page.getByText("WLAN-Zugangspunkt").waitFor();
    const start = page.getByRole("button", { name: "WLAN starten" });
    if (await start.count()) {
      await start.click();
      await page.getByText("WLAN aktiv").waitFor();
    }
    await shot(page, "web-system-ap.png");

    const state = await context.storageState();
    const mobile = await browser.newContext({
      viewport: { width: 390, height: 844 },
      colorScheme: "dark",
      storageState: state,
    });
    const mobilePage = await mobile.newPage();
    await mobilePage.goto(`${BASE}/`);
    await mobilePage.getByRole("heading", { name: "Übersicht" }).waitFor();
    await shot(mobilePage, "web-dashboard-mobile.png");
    await mobilePage.goto(`${BASE}/users`);
    await mobilePage.getByRole("heading", { name: "Benutzer" }).waitFor();
    await shot(mobilePage, "web-users-mobile.png");
    await mobile.close();
    await context.close();
  } finally {
    await browser.close();
    stopChildren();
  }
}

main().catch((error) => {
  console.error(error);
  stopChildren();
  process.exitCode = 1;
});
