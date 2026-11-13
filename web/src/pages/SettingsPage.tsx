import { useState } from "react";
import { api } from "../api/endpoints";
import type { DisplaySettings, Settings, UserSort } from "../api/types";
import { Alert } from "../components/Alert";
import { MoneyInput } from "../components/MoneyInput";
import { PageHeader } from "../components/PageHeader";
import { de } from "../i18n/de";
import { errorText } from "../lib/errors";
import { formatEuro, inputFromCents, parseMoneyInput } from "../lib/format";
import { useAsync } from "../lib/useAsync";
import { useWriteId } from "../lib/useWriteId";

export function SettingsPage() {
  const settings = useAsync<Settings>(() => api.settings(), []);

  return (
    <div className="space-y-5">
      <PageHeader title={de.settings} subtitle={de.settingsSubtitle} />
      {settings.error ? (
        <Alert tone="bad" action={<button className="btn" onClick={settings.reload} type="button">{de.retry}</button>}>
          {settings.error}
        </Alert>
      ) : null}
      {settings.loading && !settings.data ? <div className="muted">{de.loading}</div> : null}
      {settings.data ? <SettingsForm initial={settings.data} /> : null}
    </div>
  );
}

function SettingsForm({ initial }: { initial: Settings }) {
  const [display, setDisplay] = useState<DisplaySettings>(initial.display);
  const [apSsid, setApSsid] = useState(initial.network.ap_ssid);
  const [apPassword, setApPassword] = useState("");
  const [apWindowMinutes, setApWindowMinutes] = useState(
    String(Math.round((initial.network.ap_window_s || 900) / 60)),
  );
  const [hostname, setHostname] = useState(initial.network.hostname);
  const [staEnabled, setStaEnabled] = useState(initial.network.sta_enabled);
  const [staSsid, setStaSsid] = useState(initial.network.sta_ssid);
  const [staPassword, setStaPassword] = useState("");
  const [naptEnabled, setNaptEnabled] = useState(initial.network.napt_enabled);
  const [timezone, setTimezone] = useState(initial.time.timezone);
  const [sntpEnabled, setSntpEnabled] = useState(initial.time.sntp_enabled);
  const [sntpServer, setSntpServer] = useState(initial.time.sntp_server);
  const [guestEnabled, setGuestEnabled] = useState(initial.app.guest_enabled);
  // Older firmware replies without the field; fall back to the default order.
  const [userSort, setUserSort] = useState<UserSort>(
    initial.app.user_sort === "usage" ? "usage" : "creation",
  );
  const [debtLimit, setDebtLimit] = useState(inputFromCents(initial.app.max_open_debt_cents));
  const [lowBalance, setLowBalance] = useState(inputFromCents(initial.app.low_balance_warning_cents));

  const [currentPassword, setCurrentPassword] = useState("");
  const [newPassword, setNewPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");

  const [notice, setNotice] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState<"display" | "general" | "password" | "test" | "">("");

  const displayWrite = useWriteId();
  const generalWrite = useWriteId();
  const passwordWrite = useWriteId();
  const testWrite = useWriteId();

  async function saveDisplay(event: React.FormEvent) {
    event.preventDefault();
    setBusy("display");
    setError("");
    setNotice("");
    try {
      const saved = await api.saveDisplay(display, displayWrite.id);
      displayWrite.renew();
      setDisplay(saved);
      setNotice(de.displaySaved);
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy("");
    }
  }

  async function showTestPattern() {
    setBusy("test");
    setError("");
    setNotice("");
    try {
      await api.testPattern(testWrite.id);
      testWrite.renew();
      setNotice(de.testPatternSent);
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy("");
    }
  }

  async function saveGeneral(event: React.FormEvent) {
    event.preventDefault();
    const debt = debtLimit.trim() ? parseMoneyInput(debtLimit) : { cents: 0, valid: true };
    const low = lowBalance.trim() ? parseMoneyInput(lowBalance) : { cents: 0, valid: true };
    if (!debt.valid || !low.valid) {
      setError(de.invalidAmount);
      setNotice("");
      return;
    }
    setBusy("general");
    setError("");
    setNotice("");
    const payload: Settings = {
      display,
      network: {
        ap_ssid: apSsid.trim(),
        ap_window_s: Math.max(60, (Number.parseInt(apWindowMinutes, 10) || 15) * 60),
        hostname: hostname.trim(),
        sta_enabled: staEnabled,
        sta_ssid: staSsid.trim(),
        napt_enabled: naptEnabled,
        ap_password: apPassword || undefined,
        sta_password: staPassword || undefined,
      },
      time: {
        timezone: timezone.trim() || "Europe/Berlin",
        sntp_enabled: sntpEnabled,
        sntp_server: sntpServer.trim(),
      },
      app: {
        currency: "EUR",
        locale: "de-DE",
        guest_enabled: guestEnabled,
        user_sort: userSort,
        max_open_debt_cents: debt.cents,
        low_balance_warning_cents: low.cents,
      },
    };
    try {
      await api.saveSettings(payload, generalWrite.id);
      generalWrite.renew();
      setApPassword("");
      setStaPassword("");
      setNotice(de.settingsSaved);
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy("");
    }
  }

  async function changePassword(event: React.FormEvent) {
    event.preventDefault();
    setError("");
    setNotice("");
    if (newPassword.length < 10) {
      setError(de.passwordTooShort);
      return;
    }
    if (newPassword !== confirmPassword) {
      setError(de.passwordMismatch);
      return;
    }
    setBusy("password");
    try {
      await api.changePassword(currentPassword, newPassword, passwordWrite.id);
      passwordWrite.renew();
      setCurrentPassword("");
      setNewPassword("");
      setConfirmPassword("");
      setNotice(de.passwordChanged);
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy("");
    }
  }

  return (
    <div className="space-y-4">
      {notice ? <Alert tone="good">{notice}</Alert> : null}
      {error ? <Alert tone="bad">{error}</Alert> : null}

      <form className="card space-y-4 p-4 sm:p-5" onSubmit={saveDisplay}>
        <h2 className="font-medium">{de.displaySection}</h2>
        <div className="space-y-2">
          <span className="label">{de.rotation}</span>
          <div className="flex flex-wrap gap-1.5">
            {([0, 90, 180, 270] as const).map((rotation) => (
              <button
                key={rotation}
                type="button"
                className={`rounded-xl border px-4 py-2 text-sm ${
                  display.rotation === rotation
                    ? "border-amber-400 bg-amber-400 font-semibold text-espresso-950"
                    : "border-cream-100/10 text-cream-100/80"
                }`}
                onClick={() => setDisplay({ ...display, rotation })}
              >
                {rotation}°
              </button>
            ))}
          </div>
        </div>

        <div className="grid gap-4 sm:grid-cols-2">
          <label className="block space-y-2">
            <span className="label">{de.brightnessActive}: {display.brightness_active} %</span>
            <input
              type="range"
              min={5}
              max={100}
              step={1}
              value={display.brightness_active}
              onChange={(event) => setDisplay({ ...display, brightness_active: Number(event.target.value) })}
              className="w-full accent-amber-400"
            />
          </label>
          <label className="block space-y-2">
            <span className="label">{de.brightnessIdle}: {display.brightness_idle} %</span>
            <input
              type="range"
              min={1}
              max={100}
              step={1}
              value={display.brightness_idle}
              onChange={(event) => setDisplay({ ...display, brightness_idle: Number(event.target.value) })}
              className="w-full accent-amber-400"
            />
          </label>
        </div>

        <div className="grid gap-3 sm:grid-cols-2">
          <label className="block space-y-1.5">
            <span className="label">{de.dimTimeout} ({de.seconds})</span>
            <input
              className="input mono"
              type="number"
              min={0}
              max={86400}
              value={display.dim_timeout_s}
              onChange={(event) => setDisplay({ ...display, dim_timeout_s: Number(event.target.value) })}
            />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.screenOffTimeout} ({de.seconds})</span>
            <input
              className="input mono"
              type="number"
              min={0}
              max={86400}
              value={display.screen_off_timeout_s}
              onChange={(event) => setDisplay({ ...display, screen_off_timeout_s: Number(event.target.value) })}
            />
          </label>
        </div>

        <div className="flex flex-wrap gap-2">
          <button className="btn btn-primary" type="submit" disabled={busy !== ""}>
            {busy === "display" ? de.saving : de.save}
          </button>
          <button className="btn" type="button" onClick={showTestPattern} disabled={busy !== ""}>
            {busy === "test" ? de.loading : de.testPattern}
          </button>
        </div>
      </form>

      <form className="card space-y-5 p-4 sm:p-5" onSubmit={saveGeneral}>
        <section className="space-y-3">
          <h2 className="font-medium">{de.networkSection}</h2>
          <div className="grid gap-3 sm:grid-cols-2">
            <label className="block space-y-1.5">
              <span className="label">{de.apSsid}</span>
              <input className="input" value={apSsid} onChange={(event) => setApSsid(event.target.value)} maxLength={32} />
            </label>
            <label className="block space-y-1.5">
              <span className="label">{de.apPassword}</span>
              <input
                className="input"
                type="password"
                value={apPassword}
                onChange={(event) => setApPassword(event.target.value)}
                placeholder={de.secretHint}
                autoComplete="new-password"
              />
            </label>
            <label className="block space-y-1.5">
              <span className="label">{de.apWindow} ({de.minutes})</span>
              <input
                className="input mono"
                type="number"
                min={1}
                max={60}
                value={apWindowMinutes}
                onChange={(event) => setApWindowMinutes(event.target.value)}
              />
            </label>
            <label className="block space-y-1.5">
              <span className="label">{de.hostname}</span>
              <input className="input" value={hostname} onChange={(event) => setHostname(event.target.value)} maxLength={32} />
            </label>
          </div>

          <label className="flex min-h-11 items-center gap-2 text-sm">
            <input
              type="checkbox"
              className="h-4 w-4 accent-amber-400"
              checked={staEnabled}
              onChange={(event) => setStaEnabled(event.target.checked)}
            />
            {de.staEnabled}
          </label>

          <div className="grid gap-3 sm:grid-cols-2">
            <label className="block space-y-1.5">
              <span className="label">{de.staSsid}</span>
              <input className="input" value={staSsid} onChange={(event) => setStaSsid(event.target.value)} maxLength={32} />
            </label>
            <label className="block space-y-1.5">
              <span className="label">{de.staPassword}</span>
              <input
                className="input"
                type="password"
                value={staPassword}
                onChange={(event) => setStaPassword(event.target.value)}
                placeholder={de.secretHint}
                autoComplete="new-password"
              />
            </label>
          </div>

          <label className="flex min-h-11 items-center gap-2 text-sm">
            <input
              type="checkbox"
              className="h-4 w-4 accent-amber-400"
              checked={naptEnabled}
              onChange={(event) => setNaptEnabled(event.target.checked)}
            />
            {de.naptEnabled}
          </label>
        </section>

        <section className="space-y-3 border-t border-cream-100/10 pt-4">
          <h2 className="font-medium">{de.timeSection}</h2>
          <div className="grid gap-3 sm:grid-cols-2">
            <label className="block space-y-1.5">
              <span className="label">{de.timezone}</span>
              <input className="input" value={timezone} onChange={(event) => setTimezone(event.target.value)} maxLength={64} />
            </label>
            <label className="block space-y-1.5">
              <span className="label">{de.sntpServer}</span>
              <input className="input" value={sntpServer} onChange={(event) => setSntpServer(event.target.value)} maxLength={64} />
            </label>
          </div>
          <label className="flex min-h-11 items-center gap-2 text-sm">
            <input
              type="checkbox"
              className="h-4 w-4 accent-amber-400"
              checked={sntpEnabled}
              onChange={(event) => setSntpEnabled(event.target.checked)}
            />
            {de.sntpEnabled}
          </label>
        </section>

        <section className="space-y-3 border-t border-cream-100/10 pt-4">
          <h2 className="font-medium">{de.appSection}</h2>
          <label className="flex min-h-11 items-center gap-2 text-sm">
            <input
              type="checkbox"
              className="h-4 w-4 accent-amber-400"
              checked={guestEnabled}
              onChange={(event) => setGuestEnabled(event.target.checked)}
            />
            {de.guestEnabled}
          </label>

          <div className="space-y-2">
            <span className="label">{de.userSortLabel}</span>
            <div className="flex flex-wrap gap-1.5" role="group" aria-label={de.userSortLabel}>
              {(["creation", "usage"] as const).map((mode) => (
                <button
                  key={mode}
                  type="button"
                  aria-pressed={userSort === mode}
                  className={`rounded-xl border px-4 py-2 text-sm ${
                    userSort === mode
                      ? "border-amber-400 bg-amber-400 font-semibold text-espresso-950"
                      : "border-cream-100/10 text-cream-100/80"
                  }`}
                  onClick={() => setUserSort(mode)}
                >
                  {mode === "creation" ? de.userSortCreation : de.userSortUsage}
                </button>
              ))}
            </div>
            <p className="muted text-xs">{de.userSortHint}</p>
          </div>
          <div className="grid gap-3 sm:grid-cols-2">
            <label className="block space-y-1.5">
              <span className="label">{de.maxOpenDebt}</span>
              <MoneyInput value={debtLimit} onChange={setDebtLimit} />
              <div className="muted text-xs">
                {debtLimit.trim() && !parseMoneyInput(debtLimit).valid
                  ? de.invalidAmount
                  : formatEuro(parseMoneyInput(debtLimit).cents)}
              </div>
            </label>
            <label className="block space-y-1.5">
              <span className="label">{de.lowBalanceWarning}</span>
              <MoneyInput value={lowBalance} onChange={setLowBalance} />
              <div className="muted text-xs">
                {lowBalance.trim() && !parseMoneyInput(lowBalance).valid
                  ? de.invalidAmount
                  : formatEuro(parseMoneyInput(lowBalance).cents)}
              </div>
            </label>
          </div>
        </section>

        <div className="flex justify-end">
          <button className="btn btn-primary" type="submit" disabled={busy !== ""}>
            {busy === "general" ? de.saving : de.saveSettings}
          </button>
        </div>
      </form>

      <form className="card space-y-4 p-4 sm:p-5" onSubmit={changePassword}>
        <h2 className="font-medium">{de.passwordSection}</h2>
        <p className="muted text-sm">{de.passwordHint}</p>
        <div className="grid gap-3 sm:grid-cols-3">
          <label className="block space-y-1.5">
            <span className="label">{de.currentPassword}</span>
            <input
              className="input"
              type="password"
              value={currentPassword}
              onChange={(event) => setCurrentPassword(event.target.value)}
              autoComplete="current-password"
              required
            />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.newPassword}</span>
            <input
              className="input"
              type="password"
              value={newPassword}
              onChange={(event) => setNewPassword(event.target.value)}
              autoComplete="new-password"
              minLength={10}
              required
            />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.confirmPassword}</span>
            <input
              className="input"
              type="password"
              value={confirmPassword}
              onChange={(event) => setConfirmPassword(event.target.value)}
              autoComplete="new-password"
              required
            />
          </label>
        </div>
        <div className="flex justify-end">
          <button className="btn btn-primary" type="submit" disabled={busy !== ""}>
            {busy === "password" ? de.saving : de.changePassword}
          </button>
        </div>
      </form>
    </div>
  );
}
