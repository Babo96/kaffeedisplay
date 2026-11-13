import http from "node:http";

const PORT = Number(process.env.MOCK_PORT ?? 8787);
const now = Math.floor(Date.now() / 1000);
const SESSION_COOKIE = "kaffee_session=mock";
const CSRF_TOKEN = "mock-csrf-token";

// ---------------------------------------------------------------------------
// Seed data
// ---------------------------------------------------------------------------

const users = [
  { id: "usr_ada", name: "Ada", color: "#D4A373", avatar_id: 1, active: true, created_at: now - 86400 * 90 },
  { id: "usr_bob", name: "Bob", color: "#8FBC8F", avatar_id: 8, active: true, created_at: now - 86400 * 60 },
  { id: "usr_cara", name: "Cara", color: "#7FA7C9", avatar_id: 15, active: true, created_at: now - 86400 * 30 },
  {
    id: "usr_dora",
    name: "Dora",
    color: "#C9A0DC",
    avatar_id: 0,
    active: false,
    created_at: now - 86400 * 200,
    archived_at: now - 86400 * 10,
  },
];

const products = [
  product("prd_kaffee", "Kaffee", "Kaffee", "cup", "#D4A373", 50, 0, true),
  product("prd_espresso", "Espresso", "Kaffee", "espresso", "#B5835A", 40, 1, true),
  product("prd_kakao", "Kakao", "Tee", "cocoa", "#9B6B43", 80, 2, true),
  product("prd_tee", "Tee", "Tee", "tea", "#8FBC8F", 30, 3, true),
  product("prd_wasser", "Wasser", "Kaltgetränke", "water", "#7FA7C9", 20, 4, false),
];

function product(id, name, category, icon, color, price_cents, sort, active) {
  return {
    id,
    name,
    category,
    icon,
    color,
    price_cents,
    sort,
    active,
    created_at: now - 86400 * 120,
    updated_at: now - 3600,
  };
}

const balances = { usr_ada: 1250, usr_bob: -350, usr_cara: 0, usr_dora: 420 };

const expenses = [
  {
    id: "exp_machine",
    ts: now - 86400 * 20,
    category: "Maschine",
    description: "Kaffeemaschine",
    vendor: "Kaffee Shop",
    amount_cents: 45000,
    note: "",
    voided: false,
  },
  {
    id: "exp_service",
    ts: now - 86400 * 5,
    category: "Service",
    description: "Wartung",
    vendor: "Technik GmbH",
    amount_cents: 8000,
    note: "Jährliche Wartung",
    voided: false,
  },
  {
    id: "exp_clean",
    ts: now - 86400 * 2,
    category: "Reinigung",
    description: "Reinigungstabletten",
    vendor: "Kaffee Shop",
    amount_cents: 1290,
    note: "",
    voided: true,
  },
];

let seq = 20;
const events = [
  event({
    ts: now - 60,
    type: "purchase",
    user_id: "usr_ada",
    product_id: "prd_kaffee",
    unit_amount_cents: 50,
    amount_cents: -50,
    payment: "balance",
    actor: "ui",
  }),
  event({
    ts: now - 300,
    type: "purchase",
    user_id: "usr_bob",
    product_id: "prd_tee",
    unit_amount_cents: 30,
    amount_cents: -30,
    payment: "balance",
    actor: "ui",
  }),
  event({
    ts: now - 600,
    type: "guest_sale",
    product_id: "prd_kakao",
    unit_amount_cents: 80,
    amount_cents: 80,
    payment: "cash",
    actor: "ui",
  }),
  event({
    ts: now - 7200,
    type: "topup",
    user_id: "usr_ada",
    amount_cents: 1000,
    payment: "cash",
    note: "Bar eingezahlt",
    actor: "admin",
  }),
  event({
    ts: now - 86400 * 2,
    type: "purchase",
    user_id: "usr_cara",
    product_id: "prd_kakao",
    unit_amount_cents: 80,
    amount_cents: -80,
    payment: "balance",
    actor: "ui",
  }),
  event({
    ts: now - 86400 * 4,
    type: "guest_sale",
    product_id: "prd_espresso",
    unit_amount_cents: 40,
    amount_cents: 40,
    payment: "cash",
    actor: "ui",
  }),
  event({
    ts: now - 86400 * 6,
    type: "purchase",
    user_id: "usr_ada",
    product_id: "prd_espresso",
    unit_amount_cents: 40,
    amount_cents: -40,
    payment: "balance",
    actor: "ui",
  }),
];

function event({ ts, type, user_id, product_id, unit_amount_cents, amount_cents, payment, note = "", actor = "admin" }) {
  const user = users.find((item) => item.id === user_id);
  const prod = products.find((item) => item.id === product_id);
  return {
    seq: ++seq,
    id: `evt_${seq}`,
    ts,
    time_source: "synced",
    type,
    user_id,
    user_name: user?.name,
    user_color: user?.color,
    product_id,
    product_name: prod?.name,
    product_category: prod?.category,
    product_icon: prod?.icon,
    product_color: prod?.color,
    unit_amount_cents,
    amount_cents,
    payment,
    note,
    actor,
    voided: false,
  };
}

function addEvent(input) {
  const created = event({ ...input, ts: input.ts ?? now });
  events.unshift(created);
  return created;
}

const summary = {
  cash_expected_cents: 5200,
  prepaid_cents: 1670,
  debts_cents: 350,
  net_liabilities_cents: 1320,
  equity_cents: 3880,
  member_sales_cents: 1600,
  guest_sales_cents: 120,
  topups_cents: 1800,
  expenses_cents: 53000,
  member_items: 24,
  guest_items: 3,
};

function recalcSummary() {
  let prepaid = 0;
  let debts = 0;
  for (const value of Object.values(balances)) {
    if (value > 0) prepaid += value;
    else debts += -value;
  }
  summary.prepaid_cents = prepaid;
  summary.debts_cents = debts;
  summary.net_liabilities_cents = prepaid - debts;
  summary.equity_cents = summary.cash_expected_cents - summary.net_liabilities_cents;
}

const cashCounts = [
  {
    id: "cnt_1",
    ts: now - 86400 * 7,
    counted_cents: 5120,
    expected_cents: 5200,
    difference_cents: -80,
    note: "Differenz geprüft",
  },
];

const device = {
  firmware_version: "0.1.0-dev",
  build_date: "2026-11-11",
  uptime_s: 86400 * 3 + 5211,
  free_heap: 184320,
  free_psram: 3400000,
  storage_used: 48210,
  storage_total: 9437184,
  temperature_c: 41.5,
  temperature_valid: true,
  mac: "f8:5b:1b:90:b8:34",
  network: {
    ap_active: false,
    ap_starting: false,
    ap_remaining_s: 0,
    ap_ssid: "Kaffeekasse-B834",
    ap_password: "",
    ap_ip: "192.168.4.1",
    sta_connected: true,
    sta_ip: "192.168.1.42",
    sta_rssi: -57,
  },
  time: {
    now,
    source: "synced",
    last_sync: now - 120,
    last_source: "browser",
    trusted: true,
  },
  last_backup_at: now - 86400 * 3,
  drinks_today: 14,
};

const settings = {
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
    max_open_debt_cents: 0,
    low_balance_warning_cents: -1000,
  },
};

const logs = [
  "[00:00:01] I (1) boot: Kaffeekasse 0.1.0-dev",
  "[00:00:02] I (2) storage: LittleFS mounted, 9.0 MB",
  "[00:00:03] I (3) net: STA connected, RSSI -57 dBm",
  "[00:00:04] I (4) time: synced from browser",
  "[00:01:12] I (5) api: GET /api/v1/stats/summary 200",
  "[00:01:30] I (6) service: purchase usr_ada prd_kaffee 50 ct",
];

let ap = {
  active: false,
  remaining_seconds: 0,
  ssid: settings.network.ap_ssid,
  password: "",
  ip: "192.168.4.1",
  url: "http://192.168.4.1",
  wifi_qr: "",
  url_qr: "",
};

// Snapshot of the seed data so Playwright can reset state between tests.
const clone = (value) => JSON.parse(JSON.stringify(value));
const seed = {
  users: clone(users),
  products: clone(products),
  balances: clone(balances),
  expenses: clone(expenses),
  events: clone(events),
  seq,
  cashCounts: clone(cashCounts),
  summary: clone(summary),
  settings: clone(settings),
  device: clone(device),
  logs: clone(logs),
  ap: clone(ap),
};

function replaceArray(target, source) {
  target.splice(0, target.length, ...clone(source));
}

function replaceObject(target, source) {
  for (const key of Object.keys(target)) delete target[key];
  Object.assign(target, clone(source));
}

function resetMock() {
  replaceArray(users, seed.users);
  replaceArray(products, seed.products);
  replaceObject(balances, seed.balances);
  replaceArray(expenses, seed.expenses);
  replaceArray(events, seed.events);
  seq = seed.seq;
  replaceArray(cashCounts, seed.cashCounts);
  replaceObject(summary, seed.summary);
  replaceObject(settings, seed.settings);
  replaceObject(device, seed.device);
  replaceArray(logs, seed.logs);
  ap = clone(seed.ap);
  notify("mock state reset");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function json(res, status, body) {
  const data = JSON.stringify(body);
  res.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(data),
  });
  res.end(data);
}

function noContent(res) {
  res.writeHead(204);
  res.end();
}

function sendCsv(res, filename, body) {
  res.writeHead(200, {
    "Content-Type": "text/csv; charset=utf-8",
    "Content-Disposition": `attachment; filename="${filename}"`,
  });
  res.end(body);
}

function readRaw(req) {
  return new Promise((resolve) => {
    let data = "";
    req.on("data", (chunk) => (data += chunk));
    req.on("end", () => resolve(data));
  });
}

async function readBody(req) {
  const raw = await readRaw(req);
  if (!raw) return {};
  try {
    return JSON.parse(raw);
  } catch {
    return {};
  }
}

function hasSession(req) {
  return (req.headers.cookie ?? "").split(";").some((part) => part.trim() === SESSION_COOKIE);
}

function userById(id) {
  return users.find((user) => user.id === id);
}

function productById(id) {
  return products.find((item) => item.id === id);
}

function expenseById(id) {
  return expenses.find((item) => item.id === id);
}

function notify(message) {
  logs.push(`[${new Date().toISOString()}] I api: ${message}`);
  if (logs.length > 500) logs.shift();
}

function apStatus() {
  return {
    ...ap,
    active: ap.active,
    ssid: ap.active ? ap.ssid : settings.network.ap_ssid,
    password: ap.active ? ap.password : "",
    // The real backend returns plain payloads; the SPA renders the QR locally.
    wifi_qr: ap.active ? `WIFI:T:WPA;S:${ap.ssid};P:${ap.password};;` : "",
    url_qr: ap.active ? ap.url : "",
  };
}

function startAp() {
  const password = "kaffee1234";
  ap = {
    active: true,
    remaining_seconds: 900,
    ssid: settings.network.ap_ssid,
    password,
    ip: "192.168.4.1",
    url: "http://192.168.4.1",
    wifi_qr: "",
    url_qr: "",
  };
  device.network.ap_active = true;
  device.network.ap_remaining_s = 900;
  device.network.ap_password = password;
  notify("ap started");
}

function stopAp() {
  ap = {
    active: false,
    remaining_seconds: 0,
    ssid: settings.network.ap_ssid,
    password: "",
    ip: "",
    url: "",
    wifi_qr: "",
    url_qr: "",
  };
  device.network.ap_active = false;
  device.network.ap_remaining_s = 0;
  device.network.ap_password = "";
  notify("ap stopped");
}

// Mirrors the firmware: prefer presets no active user has, so a small fund does
// not end up with two identical motifs, and fall back to any preset when all 30
// are taken.
function randomUserAvatar() {
  const taken = new Set(
    users.filter((u) => u.active && u.avatar_id >= 1 && u.avatar_id <= 30).map((u) => u.avatar_id),
  );
  const free = [];
  for (let id = 1; id <= 30; id += 1) if (!taken.has(id)) free.push(id);
  if (free.length === 0) return 1 + Math.floor(Math.random() * 30);
  return free[Math.floor(Math.random() * free.length)];
}

function addUser(body) {
  const id = `usr_${Math.random().toString(16).slice(2, 8)}`;
  const user = {
    id,
    name: String(body.name ?? "Neu").slice(0, 40),
    color: body.color ?? "#D4A373",
    avatar_id: Number.isInteger(body.avatar_id) && body.avatar_id >= 0 && body.avatar_id <= 30
      ? body.avatar_id
      : randomUserAvatar(),
    active: true,
    created_at: now,
  };
  users.push(user);
  balances[id] = 0;
  addEvent({ type: "user_created", user_id: id, amount_cents: 0, note: user.name });
  recalcSummary();
  return user;
}

function patchUser(id, body) {
  const user = userById(id);
  if (!user) return null;
  if (typeof body.name === "string" && body.name.trim()) user.name = body.name.trim().slice(0, 40);
  if (typeof body.color === "string") user.color = body.color;
  if (Number.isInteger(body.avatar_id) && body.avatar_id >= 0 && body.avatar_id <= 30) {
    user.avatar_id = body.avatar_id;
  }
  if (typeof body.active === "boolean") {
    user.active = body.active;
    if (body.active) delete user.archived_at;
    else user.archived_at = now;
  }
  addEvent({ type: body.active === true ? "user_updated" : "user_updated", user_id: id, amount_cents: 0, note: user.name });
  recalcSummary();
  return user;
}

// ---------------------------------------------------------------------------
// Reports
// ---------------------------------------------------------------------------

const reportKinds = ["overview", "consumption", "deposits", "expenses", "cashflow", "balances", "transactions"];

function periodBounds(period, fromParam, toParam) {
  const current = new Date();
  const toSeconds = (date) => Math.floor(date.getTime() / 1000);
  if (period === "custom" && fromParam && toParam) {
    return { from: Number(fromParam), to: Number(toParam) };
  }
  if (period === "all") return { from: undefined, to: undefined };
  if (period === "week") {
    const start = new Date(current.getFullYear(), current.getMonth(), current.getDate());
    start.setDate(start.getDate() - ((start.getDay() + 6) % 7));
    const end = new Date(start);
    end.setDate(end.getDate() + 7);
    return { from: toSeconds(start), to: toSeconds(end) };
  }
  if (period === "year") {
    return {
      from: toSeconds(new Date(current.getFullYear(), 0, 1)),
      to: toSeconds(new Date(current.getFullYear() + 1, 0, 1)),
    };
  }
  return {
    from: toSeconds(new Date(current.getFullYear(), current.getMonth(), 1)),
    to: toSeconds(new Date(current.getFullYear(), current.getMonth() + 1, 1)),
  };
}

function inRange(ts, from, to) {
  if (from !== undefined && ts < from) return false;
  if (to !== undefined && ts >= to) return false;
  return true;
}

const typeLabels = {
  purchase: "Buchung",
  guest_sale: "Gastverkauf",
  topup: "Einzahlung",
  adjustment: "Korrektur",
  void: "Storno",
  cash_count: "Kassensturz",
  expense_created: "Ausgabe",
  expense_voided: "Ausgabe storniert",
  user_created: "Benutzer angelegt",
  user_updated: "Benutzer geändert",
  user_archived: "Benutzer archiviert",
  product_created: "Getränk angelegt",
  product_updated: "Getränk geändert",
  product_archived: "Getränk archiviert",
  product_deleted: "Getränk gelöscht",
};

function buildReport(kind, period, fromParam, toParam) {
  const { from, to } = periodBounds(period, fromParam, toParam);
  const rangeEvents = events.filter((item) => inRange(item.ts, from, to));
  const rangeExpenses = expenses.filter((item) => inRange(item.ts, from, to));
  const base = {
    kind,
    period,
    from,
    to,
    time_quality: {
      trusted: device.time.trusted,
      source: device.time.source,
      last_sync: device.time.last_sync,
    },
  };

  if (kind === "overview") {
    return {
      ...base,
      cash_expected_cents: summary.cash_expected_cents,
      prepaid_cents: summary.prepaid_cents,
      debts_cents: summary.debts_cents,
      net_liabilities_cents: summary.net_liabilities_cents,
      equity_cents: summary.equity_cents,
      member_sales_cents: summary.member_sales_cents,
      guest_sales_cents: summary.guest_sales_cents,
      topups_cents: summary.topups_cents,
      expenses_cents: summary.expenses_cents,
      member_items: summary.member_items,
      guest_items: summary.guest_items,
    };
  }

  if (kind === "consumption") {
    const grouped = new Map();
    for (const item of rangeEvents) {
      if (item.type !== "purchase" && item.type !== "guest_sale") continue;
      if (item.voided) continue;
      const key = item.product_name ?? "Unbekannt";
      const row =
        grouped.get(key) ??
        {
          product_name: key,
          product_category: item.product_category ?? "–",
          quantity: 0,
          member_revenue_cents: 0,
          guest_revenue_cents: 0,
          revenue_cents: 0,
        };
      row.quantity += 1;
      const amount = Math.abs(item.amount_cents ?? 0);
      if (item.type === "guest_sale") row.guest_revenue_cents += amount;
      else row.member_revenue_cents += amount;
      row.revenue_cents += amount;
      grouped.set(key, row);
    }
    const rows = Array.from(grouped.values()).sort((a, b) => b.revenue_cents - a.revenue_cents);
    return {
      ...base,
      columns: [
        { key: "product_name", label: "Getränk", type: "text" },
        { key: "product_category", label: "Kategorie", type: "text" },
        { key: "quantity", label: "Menge", type: "number" },
        { key: "member_revenue_cents", label: "Mitglieder", type: "money" },
        { key: "guest_revenue_cents", label: "Gäste", type: "money" },
        { key: "revenue_cents", label: "Umsatz", type: "money" },
      ],
      rows,
      totals: {
        quantity: rows.reduce((sum, row) => sum + row.quantity, 0),
        member_revenue_cents: rows.reduce((sum, row) => sum + row.member_revenue_cents, 0),
        guest_revenue_cents: rows.reduce((sum, row) => sum + row.guest_revenue_cents, 0),
        revenue_cents: rows.reduce((sum, row) => sum + row.revenue_cents, 0),
      },
    };
  }

  if (kind === "deposits") {
    const rows = rangeEvents
      .filter((item) => item.type === "topup" && !item.voided)
      .map((item) => ({
        ts: item.ts,
        user_name: item.user_name ?? "–",
        amount_cents: item.amount_cents ?? 0,
        note: item.note ?? "",
      }));
    return {
      ...base,
      columns: [
        { key: "ts", label: "Datum", type: "datetime" },
        { key: "user_name", label: "Benutzer", type: "text" },
        { key: "amount_cents", label: "Betrag", type: "money" },
        { key: "note", label: "Notiz", type: "text" },
      ],
      rows,
      totals: { amount_cents: rows.reduce((sum, row) => sum + row.amount_cents, 0) },
    };
  }

  if (kind === "expenses") {
    const rows = rangeExpenses.map((item) => ({
      ts: item.ts,
      category: item.category,
      description: item.description,
      vendor: item.vendor,
      amount_cents: item.amount_cents,
      voided: item.voided,
    }));
    return {
      ...base,
      columns: [
        { key: "ts", label: "Datum", type: "date" },
        { key: "category", label: "Kategorie", type: "text" },
        { key: "description", label: "Beschreibung", type: "text" },
        { key: "vendor", label: "Händler", type: "text" },
        { key: "amount_cents", label: "Betrag", type: "money" },
        { key: "voided", label: "Storniert", type: "boolean" },
      ],
      rows,
      totals: {
        amount_cents: rows.filter((row) => !row.voided).reduce((sum, row) => sum + row.amount_cents, 0),
      },
    };
  }

  if (kind === "cashflow") {
    const rows = [];
    for (const item of rangeEvents) {
      if (item.voided) continue;
      if (item.type === "topup") {
        rows.push({ ts: item.ts, type: "Einzahlung", description: item.user_name ?? "", amount_cents: item.amount_cents ?? 0 });
      } else if (item.type === "guest_sale") {
        rows.push({ ts: item.ts, type: "Gastverkauf", description: item.product_name ?? "", amount_cents: item.amount_cents ?? 0 });
      }
    }
    for (const item of rangeExpenses) {
      if (item.voided) continue;
      rows.push({ ts: item.ts, type: "Ausgabe", description: item.description, amount_cents: -item.amount_cents });
    }
    rows.sort((a, b) => a.ts - b.ts);
    return {
      ...base,
      columns: [
        { key: "ts", label: "Datum", type: "datetime" },
        { key: "type", label: "Art", type: "text" },
        { key: "description", label: "Beschreibung", type: "text" },
        { key: "amount_cents", label: "Betrag", type: "money" },
      ],
      rows,
      totals: {
        deposits_cents: summary.topups_cents,
        guest_sales_cents: summary.guest_sales_cents,
        expenses_cents: summary.expenses_cents,
        cash_expected_cents: summary.cash_expected_cents,
      },
    };
  }

  if (kind === "balances") {
    const rows = users.map((user) => {
      const balance = balances[user.id] ?? 0;
      return {
        user_name: user.name,
        status: user.active ? "Aktiv" : "Inaktiv",
        balance_cents: balance,
        prepaid_cents: Math.max(balance, 0),
        debts_cents: Math.max(-balance, 0),
      };
    });
    return {
      ...base,
      columns: [
        { key: "user_name", label: "Benutzer", type: "text" },
        { key: "status", label: "Status", type: "text" },
        { key: "balance_cents", label: "Saldo", type: "money" },
        { key: "prepaid_cents", label: "Guthaben", type: "money" },
        { key: "debts_cents", label: "Offen", type: "money" },
      ],
      rows,
      totals: {
        balance_cents: rows.reduce((sum, row) => sum + row.balance_cents, 0),
        prepaid_cents: summary.prepaid_cents,
        debts_cents: summary.debts_cents,
      },
    };
  }

  const rows = rangeEvents.map((item) => ({
    ts: item.ts,
    type: typeLabels[item.type] ?? item.type,
    user_name: item.user_name ?? "–",
    product_name: item.product_name ?? "–",
    amount_cents: item.amount_cents ?? 0,
    payment: item.payment === "balance" ? "Guthaben" : item.payment === "cash" ? "Bar (Gast)" : "–",
    voided: item.voided,
  }));
  return {
    ...base,
    columns: [
      { key: "ts", label: "Zeitpunkt", type: "datetime" },
      { key: "type", label: "Art", type: "text" },
      { key: "user_name", label: "Benutzer", type: "text" },
      { key: "product_name", label: "Getränk", type: "text" },
      { key: "amount_cents", label: "Betrag", type: "money" },
      { key: "payment", label: "Zahlungsart", type: "text" },
      { key: "voided", label: "Storniert", type: "boolean" },
    ],
    rows,
    totals: { amount_cents: rows.reduce((sum, row) => sum + row.amount_cents, 0) },
  };
}

function formatGermanDate(seconds) {
  return new Date(seconds * 1000).toLocaleDateString("de-DE", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
  });
}

function formatGermanDateTime(seconds) {
  return new Date(seconds * 1000).toLocaleString("de-DE", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  });
}

function csvCell(value, delimiter) {
  const text = String(value ?? "");
  let safe = text;
  if (/^[=+\-@]/.test(safe) && !/^-?\d+([.,]\d+)?$/.test(safe)) safe = `'${safe}`;
  if (safe.includes('"') || safe.includes(delimiter) || safe.includes("\n")) {
    safe = `"${safe.replaceAll('"', '""')}"`;
  }
  return safe;
}

function toCsv(report, format) {
  const delimiter = format === "csv_excel" ? ";" : ",";
  const columns = report.columns ?? [];
  const metaKeys = new Set(["kind", "period", "from", "to", "time_quality", "columns", "rows", "sections", "totals"]);
  const flat = Object.fromEntries(
    Object.entries(report).filter(
      ([key, value]) => !metaKeys.has(key) && (value === null || typeof value !== "object"),
    ),
  );
  const summary = { ...flat, ...(report.totals ?? {}) };
  if (columns.length === 0 && Object.keys(summary).length > 0) {
    const labels = {
      cash_expected_cents: "Kassenstand",
      prepaid_cents: "Guthaben",
      debts_cents: "Offene Beträge",
      net_liabilities_cents: "Nettoverbindlichkeit",
      equity_cents: "Differenz",
      member_sales_cents: "Verbrauch Mitglieder",
      guest_sales_cents: "Gastumsatz",
      topups_cents: "Einzahlungen",
      expenses_cents: "Ausgaben",
      member_items: "Getränke Mitglieder",
      guest_items: "Getränke Gäste",
    };
    const header =
      format === "csv_excel"
        ? ["Kennzahl", "Wert"]
        : ["metric", "value"];
    const lines = [header.join(delimiter)];
    for (const [key, value] of Object.entries(summary)) {
      const label = format === "csv_excel" ? labels[key] ?? key : key;
      const formatted =
        format === "csv_excel" && key.endsWith("_cents")
          ? (Number(value) / 100).toFixed(2).replace(".", ",")
          : value;
      lines.push([csvCell(label, delimiter), csvCell(formatted, delimiter)].join(delimiter));
    }
    return lines.join("\r\n") + "\r\n";
  }
  const header =
    format === "csv_excel"
      ? columns.map((column) => csvCell(column.label ?? column.key, delimiter))
      : columns.map((column) => csvCell(column.key, delimiter));
  const lines = [header.join(delimiter)];

  const excelValue = (column, value) => {
    if (value === null || value === undefined) return "";
    if (column.type === "money") return (Number(value) / 100).toFixed(2).replace(".", ",");
    if (column.type === "date") return formatGermanDate(Number(value));
    if (column.type === "datetime") return formatGermanDateTime(Number(value));
    if (column.type === "boolean") return value ? "Ja" : "Nein";
    return value;
  };
  const rawValue = (column, value) => {
    if (value === null || value === undefined) return "";
    if (column.type === "money" || column.type === "number") return String(value);
    if (column.type === "date" || column.type === "datetime") {
      return new Date(Number(value) * 1000).toISOString();
    }
    if (column.type === "boolean") return value ? "true" : "false";
    return value;
  };

  for (const row of report.rows ?? []) {
    const values = columns.map((column) =>
      csvCell(
        format === "csv_excel" ? excelValue(column, row[column.key]) : rawValue(column, row[column.key]),
        delimiter,
      ),
    );
    lines.push(values.join(delimiter));
  }
  return lines.join("\r\n") + "\r\n";
}

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url ?? "/", `http://${req.headers.host}`);
  const path = url.pathname.replace(/\/$/, "") || "/";
  const method = req.method ?? "GET";

  if (path === "/api/v1/health") return json(res, 200, { status: "ok", version: "mock" });

  // E2E-only helpers, intentionally unauthenticated and not part of OpenAPI.
  if (path === "/api/v1/__test/reset" && method === "POST") {
    resetMock();
    return noContent(res);
  }
  if (path === "/api/v1/__test/time-untrusted" && method === "POST") {
    device.time = {
      now,
      source: "unset",
      last_sync: 0,
      last_source: "",
      trusted: false,
    };
    return json(res, 200, device.time);
  }

  if (path === "/api/v1/auth/login" && method === "POST") {
    const body = await readBody(req);
    if (body.username !== "admin" || body.password !== "kaffee") {
      return json(res, 401, { code: "auth", message: "invalid credentials" });
    }
    res.setHeader("Set-Cookie", `${SESSION_COOKIE}; Path=/; HttpOnly; SameSite=Strict`);
    // Deliberately omit csrf_token: the SPA must refresh it via /auth/me.
    return json(res, 200, {
      username: "admin",
      expires_at: now + 3600,
      must_change_password: false,
    });
  }

  if (path === "/api/v1/auth/me" && method === "GET") {
    if (!hasSession(req)) {
      return json(res, 401, { code: "unauthorized", message: "not logged in" });
    }
    return json(res, 200, {
      username: "admin",
      expires_at: now + 3600,
      csrf_token: CSRF_TOKEN,
      must_change_password: false,
    });
  }

  if (path === "/api/v1/auth/logout" && method === "POST") {
    if (!hasSession(req) || req.headers["x-csrf-token"] !== CSRF_TOKEN) {
      return json(res, 403, { code: "csrf", message: "invalid csrf token" });
    }
    res.setHeader("Set-Cookie", "kaffee_session=; Path=/; Max-Age=0");
    return noContent(res);
  }

  if (!hasSession(req) && path !== "/api/v1/health") {
    return json(res, 401, { code: "unauthorized", message: "not logged in" });
  }

  const isWrite = ["POST", "PUT", "PATCH", "DELETE"].includes(method);
  if (isWrite && req.headers["x-csrf-token"] !== CSRF_TOKEN) {
    return json(res, 403, { code: "csrf", message: "invalid csrf token" });
  }

  if (path === "/api/v1/stats/summary") return json(res, 200, summary);
  if (path === "/api/v1/device") return json(res, 200, device);
  if (path === "/api/v1/time" && method === "GET") return json(res, 200, device.time);
  if (path === "/api/v1/time/sync" && method === "POST") {
    const body = await readBody(req);
    const next = Math.floor((body.epoch_ms ?? Date.now()) / 1000);
    device.time = {
      now: next,
      source: "synced",
      last_sync: next,
      last_source: "browser",
      trusted: true,
    };
    if (body.timezone) settings.time.timezone = body.timezone;
    return json(res, 200, device.time);
  }

  if (path === "/api/v1/ap" && method === "GET") return json(res, 200, apStatus());
  if (path === "/api/v1/ap/start" && method === "POST") {
    startAp();
    return json(res, 200, apStatus());
  }
  if (path === "/api/v1/ap/stop" && method === "POST") {
    stopAp();
    return noContent(res);
  }
  if (path === "/api/v1/ap/extend" && method === "POST") {
    const body = await readBody(req);
    if (ap.active) {
      ap.remaining_seconds += Number(body.seconds ?? 600);
      device.network.ap_remaining_s = ap.remaining_seconds;
    }
    return json(res, 200, apStatus());
  }

  if (path === "/api/v1/settings" && method === "GET") return json(res, 200, settings);
  if (path === "/api/v1/settings" && method === "PUT") {
    const body = await readBody(req);
    settings.display = { ...settings.display, ...(body.display ?? {}) };
    settings.network = { ...settings.network, ...(body.network ?? {}) };
    delete settings.network.ap_password;
    delete settings.network.sta_password;
    settings.time = { ...settings.time, ...(body.time ?? {}) };
    settings.app = { ...settings.app, ...(body.app ?? {}) };
    return json(res, 200, settings);
  }
  if (path === "/api/v1/settings/display" && method === "GET") return json(res, 200, settings.display);
  if (path === "/api/v1/settings/display" && method === "PUT") {
    const body = await readBody(req);
    settings.display = { ...settings.display, ...body };
    return json(res, 200, settings.display);
  }
  if (path === "/api/v1/settings/password" && method === "POST") {
    const body = await readBody(req);
    if (body.current_password !== "kaffee" || String(body.new_password ?? "").length < 10) {
      return json(res, 400, { code: "invalid", message: "password invalid" });
    }
    return noContent(res);
  }

  if (path === "/api/v1/display/test" && method === "POST") {
    notify("display test pattern");
    return noContent(res);
  }

  if (path === "/api/v1/users" && method === "GET") {
    return json(
      res,
      200,
      users
        .map((user) => ({
          ...user,
          balance_cents: balances[user.id] ?? 0,
          last_booking_at: now - 3600,
        }))
        .sort((a, b) => Number(b.active) - Number(a.active) || a.name.localeCompare(b.name, "de")),
    );
  }
  if (path === "/api/v1/users" && method === "POST") {
    const body = await readBody(req);
    if (!String(body.name ?? "").trim()) return json(res, 400, { code: "invalid", message: "name required" });
    return json(res, 201, addUser(body));
  }

  let match = path.match(/^\/api\/v1\/users\/([^/]+)$/);
  if (match && method === "PATCH") {
    const body = await readBody(req);
    const user = patchUser(match[1], body);
    if (!user) return json(res, 404, { code: "not_found", message: "user not found" });
    return json(res, 200, user);
  }
  match = path.match(/^\/api\/v1\/users\/([^/]+)\/archive$/);
  if (match && method === "POST") {
    const user = patchUser(match[1], { active: false });
    if (!user) return json(res, 404, { code: "not_found", message: "user not found" });
    addEvent({ type: "user_archived", user_id: user.id, amount_cents: 0, note: user.name });
    return json(res, 200, user);
  }
  match = path.match(/^\/api\/v1\/users\/([^/]+)\/topup$/);
  if (match && method === "POST") {
    const body = await readBody(req);
    const user = userById(match[1]);
    const amount = Number(body.amount_cents ?? 0);
    if (!user || amount <= 0) return json(res, 400, { code: "invalid", message: "amount invalid" });
    balances[user.id] = (balances[user.id] ?? 0) + amount;
    summary.cash_expected_cents += amount;
    summary.topups_cents += amount;
    recalcSummary();
    const created = addEvent({
      type: "topup",
      user_id: user.id,
      amount_cents: amount,
      payment: "cash",
      note: body.note ?? "",
    });
    return json(res, 200, created);
  }
  match = path.match(/^\/api\/v1\/users\/([^/]+)\/adjust$/);
  if (match && method === "POST") {
    const body = await readBody(req);
    const user = userById(match[1]);
    const amount = Number(body.amount_cents ?? 0);
    if (!user || amount === 0 || String(body.note ?? "").trim().length < 3) {
      return json(res, 400, { code: "invalid", message: "adjustment invalid" });
    }
    balances[user.id] = (balances[user.id] ?? 0) + amount;
    recalcSummary();
    const created = addEvent({
      type: "adjustment",
      user_id: user.id,
      amount_cents: amount,
      payment: "",
      note: body.note ?? "",
    });
    return json(res, 200, created);
  }
  match = path.match(/^\/api\/v1\/users\/([^/]+)\/transactions$/);
  if (match && method === "GET") {
    const limit = Math.min(Number(url.searchParams.get("limit") ?? 50), 200);
    return json(
      res,
      200,
      events
        .filter((item) => item.user_id === match[1])
        .sort((a, b) => b.seq - a.seq)
        .slice(0, limit),
    );
  }

  if (path === "/api/v1/products" && method === "GET") {
    const includeArchived = url.searchParams.get("include_archived") === "true";
    return json(res, 200, products.filter((item) => includeArchived || item.active));
  }
  if (path === "/api/v1/products" && method === "POST") {
    const body = await readBody(req);
    if (!String(body.name ?? "").trim() || !String(body.category ?? "").trim()) {
      return json(res, 400, { code: "invalid", message: "name required" });
    }
    const created = {
      id: `prd_${Math.random().toString(16).slice(2, 8)}`,
      name: String(body.name).slice(0, 40),
      category: String(body.category).slice(0, 40),
      icon: body.icon ?? "cup",
      color: body.color ?? "#D4A373",
      price_cents: Number(body.price_cents ?? 0),
      sort: Number(body.sort ?? products.length),
      active: body.active !== false,
      created_at: now,
      updated_at: now,
    };
    products.push(created);
    addEvent({ type: "product_created", product_id: created.id, unit_amount_cents: created.price_cents, amount_cents: 0 });
    return json(res, 201, created);
  }
  match = path.match(/^\/api\/v1\/products\/([^/]+)$/);
  if (match && method === "PATCH") {
    const item = productById(match[1]);
    if (!item) return json(res, 404, { code: "not_found", message: "product not found" });
    const body = await readBody(req);
    const missing = ["name", "category", "price_cents", "icon", "color", "sort", "active"].filter(
      (key) => body[key] === undefined,
    );
    if (missing.length > 0) {
      return json(res, 400, {
        code: "invalid_product_input",
        message: `PATCH /products requires full ProductInput; missing: ${missing.join(", ")}`,
      });
    }
    for (const key of ["name", "category", "icon", "color", "price_cents", "sort", "active"]) {
      item[key] = body[key];
    }
    item.updated_at = now;
    addEvent({ type: "product_updated", product_id: item.id, unit_amount_cents: item.price_cents, amount_cents: 0 });
    return json(res, 200, item);
  }
  if (match && method === "DELETE") {
    const index = products.findIndex((item) => item.id === match[1]);
    if (index < 0) return json(res, 404, { code: "not_found", message: "product not found" });
    const [removed] = products.splice(index, 1);
    addEvent({ type: "product_deleted", product_id: removed.id, unit_amount_cents: removed.price_cents, amount_cents: 0 });
    return noContent(res);
  }

  if (path === "/api/v1/expenses" && method === "GET") {
    const includeVoided = url.searchParams.get("include_voided") === "true";
    return json(res, 200, expenses.filter((item) => includeVoided || !item.voided));
  }
  if (path === "/api/v1/expenses" && method === "POST") {
    const body = await readBody(req);
    const amount = Number(body.amount_cents ?? 0);
    if (!String(body.description ?? "").trim() || amount <= 0) {
      return json(res, 400, { code: "invalid", message: "expense invalid" });
    }
    const created = {
      id: `exp_${Math.random().toString(16).slice(2, 8)}`,
      ts: now,
      category: String(body.category ?? "Sonstiges").slice(0, 40),
      description: String(body.description).slice(0, 120),
      vendor: String(body.vendor ?? "").slice(0, 80),
      amount_cents: amount,
      note: String(body.note ?? "").slice(0, 300),
      voided: false,
    };
    expenses.unshift(created);
    summary.cash_expected_cents -= amount;
    summary.expenses_cents += amount;
    recalcSummary();
    addEvent({
      type: "expense_created",
      expense_id: created.id,
      expense_category: created.category,
      expense_description: created.description,
      expense_vendor: created.vendor,
      amount_cents: -amount,
      note: created.note,
    });
    return json(res, 201, created);
  }
  match = path.match(/^\/api\/v1\/expenses\/([^/]+)$/);
  if (match && method === "PATCH") {
    const item = expenseById(match[1]);
    if (!item) return json(res, 404, { code: "not_found", message: "expense not found" });
    const body = await readBody(req);
    const oldAmount = item.amount_cents;
    for (const key of ["category", "description", "vendor", "amount_cents", "note"]) {
      if (body[key] !== undefined) item[key] = body[key];
    }
    if (!item.voided) {
      const delta = item.amount_cents - oldAmount;
      summary.cash_expected_cents -= delta;
      summary.expenses_cents += delta;
      recalcSummary();
    }
    addEvent({
      type: "expense_updated",
      expense_id: item.id,
      expense_category: item.category,
      expense_description: item.description,
      amount_cents: 0,
    });
    return json(res, 200, item);
  }
  match = path.match(/^\/api\/v1\/expenses\/([^/]+)\/void$/);
  if (match && method === "POST") {
    const item = expenseById(match[1]);
    if (!item) return json(res, 404, { code: "not_found", message: "expense not found" });
    if (item.voided) return json(res, 409, { code: "conflict", message: "already voided" });
    await readBody(req);
    item.voided = true;
    summary.cash_expected_cents += item.amount_cents;
    summary.expenses_cents -= item.amount_cents;
    recalcSummary();
    const created = addEvent({
      type: "expense_voided",
      expense_id: item.id,
      expense_category: item.category,
      expense_description: item.description,
      amount_cents: item.amount_cents,
    });
    return json(res, 200, created);
  }

  if (path === "/api/v1/cash-counts" && method === "GET") {
    return json(res, 200, cashCounts.slice().sort((a, b) => b.ts - a.ts));
  }
  if (path === "/api/v1/cash-counts" && method === "POST") {
    const body = await readBody(req);
    const counted = Number(body.counted_cents ?? -1);
    if (counted < 0) return json(res, 400, { code: "invalid", message: "counted invalid" });
    const created = {
      id: `cnt_${Math.random().toString(16).slice(2, 8)}`,
      ts: now,
      counted_cents: counted,
      expected_cents: summary.cash_expected_cents,
      difference_cents: counted - summary.cash_expected_cents,
      note: String(body.note ?? "").slice(0, 200),
    };
    cashCounts.unshift(created);
    addEvent({
      type: "cash_count",
      counted_cents: created.counted_cents,
      expected_cents: created.expected_cents,
      amount_cents: created.difference_cents,
      note: created.note,
    });
    return json(res, 201, created);
  }

  if (path === "/api/v1/transactions" && method === "GET") {
    const userId = url.searchParams.get("user_id");
    const type = url.searchParams.get("type");
    const from = url.searchParams.get("from");
    const to = url.searchParams.get("to");
    const limit = Math.min(Number(url.searchParams.get("limit") ?? 100), 500);
    const offset = Number(url.searchParams.get("offset") ?? 0);
    const filtered = events
      .filter((item) => {
        if (userId && item.user_id !== userId) return false;
        if (type && item.type !== type) return false;
        if (from && item.ts < Number(from)) return false;
        if (to && item.ts >= Number(to)) return false;
        return true;
      })
      .sort((a, b) => b.seq - a.seq)
      .slice(offset, offset + limit);
    return json(res, 200, filtered);
  }
  match = path.match(/^\/api\/v1\/transactions\/([^/]+)\/void$/);
  if (match && method === "POST") {
    const item = events.find((entry) => entry.id === match[1]);
    if (!item) return json(res, 404, { code: "not_found", message: "event not found" });
    if (item.voided) return json(res, 409, { code: "conflict", message: "already voided" });
    const body = await readBody(req);
    item.voided = true;
    if (item.user_id) {
      balances[item.user_id] = (balances[item.user_id] ?? 0) - (item.amount_cents ?? 0);
    }
    if (item.type === "guest_sale") summary.cash_expected_cents -= item.amount_cents ?? 0;
    if (item.type === "topup") {
      summary.cash_expected_cents -= item.amount_cents ?? 0;
      summary.topups_cents -= item.amount_cents ?? 0;
    }
    recalcSummary();
    const created = addEvent({
      type: "void",
      user_id: item.user_id,
      product_id: item.product_id,
      amount_cents: -(item.amount_cents ?? 0),
      payment: item.payment,
      note: body.reason ?? "",
      reverses: item.id,
    });
    return json(res, 200, created);
  }

  if (path === "/api/v1/logs" && method === "GET") {
    const limit = Math.min(Number(url.searchParams.get("limit") ?? 200), 500);
    return json(res, 200, { lines: logs.slice(-limit) });
  }

  if (path === "/api/v1/stats/leaderboard") {
    const month = url.searchParams.get("month") ?? "";
    return json(res, 200, {
      from: now - 86400 * 10,
      to: now,
      guest_count: 4,
      entries: [
        { user_id: "usr_ada", name: "Ada", color: "#D4A373", count: 23 },
        { user_id: "usr_bob", name: "Bob", color: "#8FBC8F", count: 17 },
        { user_id: "usr_cara", name: "Cara", color: "#7FA7C9", count: 9 },
      ],
      month,
    });
  }

  if (path.startsWith("/api/v1/reports/")) {
    const kind = path.split("/")[4];
    if (!reportKinds.includes(kind)) {
      return json(res, 404, { code: "not_found", message: "unknown report" });
    }
    const period = url.searchParams.get("period") ?? "month";
    const from = url.searchParams.get("from") ?? undefined;
    const to = url.searchParams.get("to") ?? undefined;
    const format = url.searchParams.get("format") ?? "json";
    if (format === "csv_excel" || format === "csv_raw") {
      const report = buildReport(kind, period, from, to);
      const body = toCsv(report, format);
      const month = new Date().toISOString().slice(0, 7);
      const prefix = format === "csv_excel" ? "\uFEFF" : "";
      const sep = format === "csv_excel" ? "sep=;\r\n" : "";
      return sendCsv(res, `kaffeekasse_${month}_${kind}.csv`, prefix + sep + body);
    }
    return json(res, 200, buildReport(kind, period, from, to));
  }

  if (path === "/api/v1/backup" && method === "GET") {
    return json(res, 200, {
      created_at: now,
      version: 1,
      users,
      products,
      expenses,
      events,
      cash_counts: cashCounts,
      settings,
    });
  }
  if (path === "/api/v1/restore" && method === "POST") {
    const body = await readBody(req);
    if (!body || typeof body !== "object" || !Array.isArray(body.users)) {
      return json(res, 400, { code: "invalid", message: "invalid backup" });
    }
    notify("restore from backup");
    return noContent(res);
  }
  if (path === "/api/v1/ota" && method === "POST") {
    await readRaw(req);
    notify("ota upload accepted");
    return json(res, 200, { status: "ok", reboot: true });
  }
  if (path === "/api/v1/factory-reset" && method === "POST") {
    notify("factory reset requested");
    return noContent(res);
  }

  return json(res, 404, { code: "not_found", message: `Mock has no route ${method} ${path}` });
});

server.listen(PORT, () => {
  console.log(`Kaffeekasse mock API on http://localhost:${PORT}/api/v1`);
});
