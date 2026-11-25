import { BASE, request } from "./client";
import type {
  ApStatus,
  CashCount,
  DeviceStatus,
  DisplaySettings,
  Event,
  Expense,
  ExpenseInput,
  LogsResponse,
  LongTermStats,
  Product,
  ProductInput,
  ReportFormat,
  ReportKind,
  ReportPayload,
  ReportPeriod,
  Session,
  Settings,
  Summary,
  TimeInfo,
  User,
  UserInput,
  UserWithBalance,
} from "./types";

type QueryValue = string | number | boolean | undefined;

function qs(params: Record<string, QueryValue>): string {
  const search = new URLSearchParams();
  for (const [key, value] of Object.entries(params)) {
    if (value !== undefined && value !== "") search.set(key, String(value));
  }
  const text = search.toString();
  return text ? `?${text}` : "";
}

export interface ReportRequest {
  kind: ReportKind;
  period: ReportPeriod;
  from?: number;
  to?: number;
}

export const api = {
  // --- system / dashboard -------------------------------------------------
  summary: () => request<Summary>("/stats/summary"),
  longTermStats: () => request<LongTermStats>("/stats/longterm"),
  device: () => request<DeviceStatus>("/device"),
  time: () => request<TimeInfo>("/time"),
  syncTime: (epochMs: number, timezone: string) =>
    request<TimeInfo>("/time/sync", {
      method: "POST",
      body: JSON.stringify({ epoch_ms: epochMs, timezone }),
    }),
  logs: (limit = 200) => request<LogsResponse>(`/logs${qs({ limit })}`),
  transactions: (params: { limit?: number; offset?: number; user_id?: string; type?: string } = {}) =>
    request<Event[]>(`/transactions${qs(params)}`),

  // --- auth ---------------------------------------------------------------
  session: () => request<Session>("/auth/me"),

  // --- users ---------------------------------------------------------------
  users: () => request<UserWithBalance[]>("/users"),
  createUser: (input: UserInput, requestId: string) =>
    request<User>("/users", {
      method: "POST",
      body: JSON.stringify(input),
      requestId,
    }),
  updateUser: (id: string, input: Partial<UserInput>, requestId: string) =>
    request<User>(`/users/${encodeURIComponent(id)}`, {
      method: "PATCH",
      body: JSON.stringify(input),
      requestId,
    }),
  archiveUser: (id: string, requestId: string) =>
    request<User>(`/users/${encodeURIComponent(id)}/archive`, {
      method: "POST",
      requestId,
    }),
  topup: (id: string, amountCents: number, note: string, requestId: string) =>
    request<Event>(`/users/${encodeURIComponent(id)}/topup`, {
      method: "POST",
      body: JSON.stringify({ amount_cents: amountCents, note }),
      requestId,
    }),
  adjust: (id: string, amountCents: number, note: string, requestId: string) =>
    request<Event>(`/users/${encodeURIComponent(id)}/adjust`, {
      method: "POST",
      body: JSON.stringify({ amount_cents: amountCents, note }),
      requestId,
    }),
  userTransactions: (id: string, limit = 100) =>
    request<Event[]>(`/users/${encodeURIComponent(id)}/transactions${qs({ limit })}`),

  // --- products ------------------------------------------------------------
  products: (includeArchived = true) =>
    request<Product[]>(`/products${qs({ include_archived: includeArchived })}`),
  createProduct: (input: ProductInput, requestId: string) =>
    request<Product>("/products", {
      method: "POST",
      body: JSON.stringify(input),
      requestId,
    }),
  updateProduct: (id: string, input: ProductInput, requestId: string) =>
    request<Product>(`/products/${encodeURIComponent(id)}`, {
      method: "PATCH",
      body: JSON.stringify(input),
      requestId,
    }),
  deleteProduct: (id: string, requestId: string) =>
    request<void>(`/products/${encodeURIComponent(id)}`, {
      method: "DELETE",
      requestId,
    }),

  // --- expenses ------------------------------------------------------------
  expenses: (includeVoided = true) =>
    request<Expense[]>(`/expenses${qs({ include_voided: includeVoided })}`),
  createExpense: (input: ExpenseInput, requestId: string) =>
    request<Expense>("/expenses", {
      method: "POST",
      body: JSON.stringify(input),
      requestId,
    }),
  updateExpense: (id: string, input: ExpenseInput, requestId: string) =>
    request<Expense>(`/expenses/${encodeURIComponent(id)}`, {
      method: "PATCH",
      body: JSON.stringify(input),
      requestId,
    }),
  voidExpense: (id: string, reason: string, requestId: string) =>
    request<Event>(`/expenses/${encodeURIComponent(id)}/void`, {
      method: "POST",
      body: JSON.stringify({ reason }),
      requestId,
    }),

  // --- cash ----------------------------------------------------------------
  cashCounts: () => request<CashCount[]>("/cash-counts"),
  createCashCount: (countedCents: number, note: string, requestId: string) =>
    request<CashCount>("/cash-counts", {
      method: "POST",
      body: JSON.stringify({ counted_cents: countedCents, note }),
      requestId,
    }),

  // --- reports -------------------------------------------------------------
  report: (params: ReportRequest) =>
    request<ReportPayload>(`/reports/${params.kind}${reportQuery(params, "json")}`),
  /**
   * Same-origin download URL for a report CSV.
   *
   * The browser performs the request itself and uses the server's
   * `Content-Disposition` filename. Fetching into a Blob first breaks mobile
   * Safari: the click would happen after an `await`, outside the user
   * activation window, and the download is dropped.
   */
  reportCsvUrl: (params: ReportRequest, format: Exclude<ReportFormat, "json">) =>
    `${BASE}/reports/${params.kind}${reportQuery(params, format)}`,

  // --- settings ------------------------------------------------------------
  settings: () => request<Settings>("/settings"),
  saveSettings: (settings: Settings, requestId: string) =>
    request<Settings>("/settings", {
      method: "PUT",
      body: JSON.stringify(settings),
      requestId,
    }),
  display: () => request<DisplaySettings>("/settings/display"),
  saveDisplay: (display: DisplaySettings, requestId: string) =>
    request<DisplaySettings>("/settings/display", {
      method: "PUT",
      body: JSON.stringify(display),
      requestId,
    }),
  changePassword: (
    currentPassword: string,
    newPassword: string,
    requestId: string,
  ) =>
    request<void>("/settings/password", {
      method: "POST",
      body: JSON.stringify({
        current_password: currentPassword,
        new_password: newPassword,
      }),
      requestId,
    }),
  testPattern: (requestId: string) =>
    request<void>("/display/test", { method: "POST", requestId }),

  // --- AP / system ---------------------------------------------------------
  ap: () => request<ApStatus>("/ap"),
  startAp: (requestId: string) => request<ApStatus>("/ap/start", { method: "POST", requestId }),
  stopAp: (requestId: string) =>
    request<void>("/ap/stop", { method: "POST", requestId }),
  extendAp: (seconds: number, requestId: string) =>
    request<ApStatus>("/ap/extend", {
      method: "POST",
      body: JSON.stringify({ seconds }),
      requestId,
    }),
  backup: () => request<Record<string, unknown>>("/backup"),
  restore: (backup: unknown, requestId: string) =>
    request<void>("/restore", {
      method: "POST",
      body: JSON.stringify(backup),
      requestId,
    }),
  uploadFirmware: (file: File, requestId: string) => {
    const form = new FormData();
    form.append("firmware", file, file.name);
    return request<unknown>("/ota", {
      method: "POST",
      body: form,
      requestId,
    });
  },
  /**
   * Contract gap: `docs/api/openapi.yaml` does not define a factory-reset
   * endpoint. The SPA calls the path the backend task list assumes and
   * surfaces a clear error if the firmware does not implement it yet.
   */
  factoryReset: (requestId: string) =>
    request<void>("/factory-reset", { method: "POST", requestId }),
};

function reportQuery(params: ReportRequest, format: ReportFormat): string {
  return qs({
    period: params.period,
    from: params.from,
    to: params.to,
    format,
  });
}

export type Api = typeof api;
