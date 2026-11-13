import { useMemo, useState } from "react";
import { api } from "../api/endpoints";
import type { UserWithBalance } from "../api/types";
import { Alert } from "../components/Alert";
import { ConfirmDialog } from "../components/ConfirmDialog";
import { MoneyInput, MoneyPreview } from "../components/MoneyInput";
import { Modal } from "../components/Modal";
import { PageHeader } from "../components/PageHeader";
import { TransactionTable } from "../components/TransactionTable";
import { UserAvatar } from "../components/UserAvatar";
import { AVATAR_ART } from "../components/avatarArt.generated";
import { de } from "../i18n/de";
import { errorText } from "../lib/errors";
import {
  formatDate,
  formatEuro,
  parseMoneyInput,
} from "../lib/format";
import { useAsync } from "../lib/useAsync";
import { useWriteId } from "../lib/useWriteId";

const colors = ["#D4A373", "#8FBC8F", "#7FA7C9", "#E07A5F", "#C9A0DC", "#F2CC8F", "#9B6B43", "#A3B18A"];

const AVATAR_COUNT = AVATAR_ART.length;

/** German motif name for an avatar id (0 = initials only). */
function avatarName(id: number): string {
  if (id === 0) return de.userAvatarNone;
  return de.userAvatarNames[id - 1] ?? `${de.userAvatarPreset} ${id}`;
}

/** Accessible name: the motif name plus the stable "Motiv n" index. */
function avatarLabel(id: number): string {
  return id === 0 ? de.userAvatarNone : `${avatarName(id)} (${de.userAvatarPreset} ${id})`;
}

/** Draws a concrete avatar id, never repeating the previous one. */
function drawAvatarId(previous: number | null = null): number {
  const next = 1 + Math.floor(Math.random() * AVATAR_COUNT);
  return AVATAR_COUNT > 1 && next === previous ? (next % AVATAR_COUNT) + 1 : next;
}

type Dialog =
  | { kind: "create" }
  | { kind: "edit"; user: UserWithBalance }
  | { kind: "topup"; user: UserWithBalance }
  | { kind: "adjust"; user: UserWithBalance }
  | { kind: "history"; user: UserWithBalance }
  | { kind: "archive"; user: UserWithBalance }
  | null;

export function UsersPage() {
  const [search, setSearch] = useState("");
  const [showInactive, setShowInactive] = useState(false);
  const [dialog, setDialog] = useState<Dialog>(null);
  const [notice, setNotice] = useState("");
  const [noticeTone, setNoticeTone] = useState<"good" | "bad" | "info">("info");
  const [restoring, setRestoring] = useState("");
  const writeId = useWriteId();
  const users = useAsync<UserWithBalance[]>(() => api.users(), []);

  const filtered = useMemo(() => {
    const query = search.trim().toLowerCase();
    return (users.data ?? []).filter((user) => {
      if (!showInactive && !user.active) return false;
      if (!query) return true;
      return (user.name ?? "").toLowerCase().includes(query);
    });
  }, [users.data, search, showInactive]);

  const totals = useMemo(() => {
    const list = users.data ?? [];
    return {
      active: list.filter((user) => user.active).length,
      prepaid: list.reduce((sum, user) => sum + Math.max(user.balance_cents, 0), 0),
      debts: list.reduce((sum, user) => sum + Math.max(-user.balance_cents, 0), 0),
    };
  }, [users.data]);

  function saved(message: string) {
    setNoticeTone("good");
    setNotice(message);
    setDialog(null);
    users.reload();
  }

  async function restore(user: UserWithBalance) {
    setRestoring(user.id);
    setNotice("");
    try {
      await api.updateUser(user.id, { active: true }, writeId.id);
      writeId.renew();
      setNoticeTone("good");
      setNotice(de.userRestored);
      users.reload();
    } catch (err) {
      setNoticeTone("bad");
      setNotice(errorText(err));
    } finally {
      setRestoring("");
    }
  }

  return (
    <div className="space-y-5">
      <PageHeader
        title={de.users}
        subtitle={`${totals.active} ${de.active} · ${de.prepaid} ${formatEuro(totals.prepaid)} · ${de.debts} ${formatEuro(totals.debts)}`}
        actions={
          <button className="btn btn-primary" type="button" onClick={() => setDialog({ kind: "create" })}>
            {de.addUser}
          </button>
        }
      />

      {notice ? <Alert tone={noticeTone}>{notice}</Alert> : null}
      {users.error ? (
        <Alert tone="bad" action={<button className="btn" onClick={users.reload} type="button">{de.retry}</button>}>
          {users.error}
        </Alert>
      ) : null}

      <div className="flex flex-col gap-2 sm:flex-row sm:items-center">
        <label className="flex-1">
          <span className="sr-only">{de.searchUsers}</span>
          <input
            className="input"
            value={search}
            onChange={(event) => setSearch(event.target.value)}
            placeholder={`${de.searchUsers} …`}
            type="search"
          />
        </label>
        <label className="flex min-h-11 items-center gap-2 rounded-xl border border-cream-100/10 px-3 text-sm">
          <input
            type="checkbox"
            className="h-4 w-4 accent-amber-400"
            checked={showInactive}
            onChange={(event) => setShowInactive(event.target.checked)}
          />
          {de.showInactive}
        </label>
      </div>

      {users.loading && !users.data ? <div className="muted">{de.loading}</div> : null}

      {!users.loading && filtered.length === 0 ? (
        <div className="card p-8 text-center">
          <div className="muted">{users.data && users.data.length > 0 ? de.noResults : de.noUsers}</div>
        </div>
      ) : null}

      <ul className="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
        {filtered.map((user) => {
          const negative = user.balance_cents < 0;
          return (
            <li key={user.id} className={`card p-4 ${user.active ? "" : "opacity-70"}`}>
              <div className="flex items-start justify-between gap-2">
                <div className="flex min-w-0 items-center gap-2">
                  <UserAvatar name={user.name} color={user.color} avatarId={user.avatar_id} />
                  <div className="min-w-0">
                    <div className="truncate font-medium">{user.name}</div>
                    <div
                      className="muted text-xs"
                      title={
                        user.last_booking_at
                          ? `${de.lastBooking} ${formatDate(user.last_booking_at)}`
                          : undefined
                      }
                    >
                      {user.active ? de.active : de.inactive}
                    </div>
                  </div>
                </div>
                <div className="text-right">
                  <div className={`mono text-lg font-semibold ${negative ? "text-rust-400" : "text-sage-400"}`}>
                    {formatEuro(user.balance_cents)}
                  </div>
                  <div className="muted text-[11px]">{negative ? de.openAmount : de.balance}</div>
                </div>
              </div>

              <div className="mt-3 flex flex-wrap gap-1.5">
                <button className="btn min-h-9 px-2.5 py-1 text-xs" type="button" onClick={() => setDialog({ kind: "topup", user })}>
                  {de.recordTopup}
                </button>
                <button className="btn min-h-9 px-2.5 py-1 text-xs" type="button" onClick={() => setDialog({ kind: "adjust", user })}>
                  {de.adjustmentTitle}
                </button>
                <button className="btn min-h-9 px-2.5 py-1 text-xs" type="button" onClick={() => setDialog({ kind: "history", user })}>
                  {de.history}
                </button>
                <button className="btn min-h-9 px-2.5 py-1 text-xs" type="button" onClick={() => setDialog({ kind: "edit", user })}>
                  {de.edit}
                </button>
                {user.active ? (
                  <button
                    className="btn btn-danger min-h-9 px-2.5 py-1 text-xs"
                    type="button"
                    onClick={() => setDialog({ kind: "archive", user })}
                  >
                    {de.archive}
                  </button>
                ) : (
                  <button
                    className="btn min-h-9 px-2.5 py-1 text-xs"
                    type="button"
                    onClick={() => restore(user)}
                    disabled={restoring === user.id}
                  >
                    {restoring === user.id ? de.loading : de.restoreUser}
                  </button>
                )}
              </div>
            </li>
          );
        })}
      </ul>

      {dialog?.kind === "create" ? (
        <UserFormDialog onClose={() => setDialog(null)} onSaved={() => saved(de.userCreated)} />
      ) : null}
      {dialog?.kind === "edit" ? (
        <UserFormDialog
          user={dialog.user}
          onClose={() => setDialog(null)}
          onSaved={() => saved(de.userUpdated)}
        />
      ) : null}
      {dialog?.kind === "topup" ? (
        <TopupDialog
          user={dialog.user}
          onClose={() => setDialog(null)}
          onSaved={() => saved(de.topupSuccess)}
        />
      ) : null}
      {dialog?.kind === "adjust" ? (
        <AdjustDialog
          user={dialog.user}
          onClose={() => setDialog(null)}
          onSaved={() => saved(de.adjustSuccess)}
        />
      ) : null}
      {dialog?.kind === "history" ? (
        <HistoryDialog user={dialog.user} onClose={() => setDialog(null)} />
      ) : null}
      {dialog?.kind === "archive" ? (
        <ArchiveDialog
          user={dialog.user}
          onClose={() => setDialog(null)}
          onSaved={() => saved(de.userArchived)}
        />
      ) : null}
    </div>
  );
}

function UserFormDialog({
  user,
  onClose,
  onSaved,
}: {
  user?: UserWithBalance;
  onClose: () => void;
  onSaved: () => void;
}) {
  const [name, setName] = useState(user?.name ?? "");
  const [color, setColor] = useState(user?.color ?? colors[0]);
  // null means the server chooses a random avatar when creating a user.
  const [avatarId, setAvatarId] = useState<number | null>(user ? (user.avatar_id ?? 0) : null);
  // Concrete motif previewed in the random row; only sent once the admin rolls or picks.
  const [randomId, setRandomId] = useState(() => drawAvatarId());
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const writeId = useWriteId();

  function rerollAvatar() {
    const next = drawAvatarId(randomId);
    setRandomId(next);
    // Rolling is an explicit choice, so this concrete id travels with the request.
    setAvatarId(next);
  }

  function resetToRandom() {
    setRandomId((current) => drawAvatarId(current));
    setAvatarId(null);
  }

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    if (!name.trim()) {
      setError(de.required);
      return;
    }
    setBusy(true);
    setError("");
    try {
      if (user) {
        await api.updateUser(user.id, { name: name.trim(), color, avatar_id: avatarId ?? 0 }, writeId.id);
      } else {
        await api.createUser(
          { name: name.trim(), color, ...(avatarId === null ? {} : { avatar_id: avatarId }) },
          writeId.id,
        );
      }
      writeId.renew();
      onSaved();
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      title={user ? de.editUserTitle : de.createUserTitle}
      onClose={onClose}
      footer={
        <>
          <button className="btn" type="button" onClick={onClose} disabled={busy}>
            {de.cancel}
          </button>
          <button className="btn btn-primary" type="submit" form="user-form" disabled={busy}>
            {busy ? de.saving : de.save}
          </button>
        </>
      }
    >
      <form id="user-form" className="space-y-4" onSubmit={submit}>
        <label className="block space-y-1.5">
          <span className="label">{de.name}</span>
          <input
            className="input"
            value={name}
            onChange={(event) => setName(event.target.value)}
            maxLength={40}
            required
            autoFocus
          />
        </label>
        <div className="space-y-2">
          <span className="label">{de.userColor}</span>
          <div className="flex flex-wrap items-center gap-2">
            {colors.map((preset) => (
              <button
                key={preset}
                type="button"
                className={`h-9 w-9 rounded-full border-2 ${color === preset ? "border-cream-50" : "border-transparent"}`}
                style={{ backgroundColor: preset }}
                onClick={() => setColor(preset)}
                aria-label={preset}
              />
            ))}
          </div>
          <label className="flex w-fit items-center gap-2 text-xs">
            <input
              type="color"
              className="h-9 w-12 rounded-lg border border-cream-100/20 bg-transparent"
              value={color}
              onChange={(event) => setColor(event.target.value)}
            />
            <span className="muted">{de.userColorCustom}</span>
          </label>
        </div>
        <fieldset className="space-y-2">
          <legend className="label">{de.userAvatar}</legend>
          {!user ? (
            <div className="rounded-xl border border-cream-100/15 p-2">
              <div className="flex items-center gap-2">
                <button
                  type="button"
                  className={`flex min-w-0 flex-1 items-center gap-2 rounded-lg border-2 px-2 py-1.5 text-left ${
                    avatarId === null ? "border-cream-50" : "border-transparent"
                  }`}
                  aria-label={de.userAvatarRandom}
                  aria-pressed={avatarId === null}
                  onClick={resetToRandom}
                >
                  <UserAvatar name="" color={color} avatarId={randomId} size={32} />
                  <span className="min-w-0">
                    <span className="block text-sm">{de.userAvatarRandomShort}</span>
                    <span className="muted block text-xs">
                      {avatarId === randomId ? de.userAvatarSelected : de.userAvatarSample}: {avatarName(randomId)}
                    </span>
                  </span>
                </button>
                <button
                  type="button"
                  className="btn min-h-9 shrink-0 px-2.5 py-1 text-xs"
                  onClick={rerollAvatar}
                >
                  {de.userAvatarReroll}
                </button>
              </div>
              <p className="muted mt-1.5 text-xs">{de.userAvatarRandomHint}</p>
            </div>
          ) : null}
          <div className="grid grid-cols-5 gap-2 sm:grid-cols-6">
            {Array.from({ length: AVATAR_COUNT + 1 }, (_, id) => {
              const selected = avatarId === id;
              return (
                <button
                  key={id}
                  type="button"
                  className={`relative flex min-h-14 flex-col items-center justify-center gap-0.5 rounded-lg border-2 px-1 py-1.5 ${
                    selected ? "border-cream-50 bg-cream-50/10" : "border-cream-100/20"
                  }`}
                  aria-label={avatarLabel(id)}
                  aria-pressed={selected}
                  onClick={() => setAvatarId(id)}
                  title={avatarLabel(id)}
                >
                  {id === 0 ? <InitialsGlyph /> : <UserAvatar name="" color={color} avatarId={id} size={32} />}
                  {id === 0 ? <span className="text-xs leading-none">{de.userAvatarNoneShort}</span> : null}
                  {selected ? (
                    <span className="absolute right-0.5 top-0.5 flex h-4 w-4 items-center justify-center rounded-full bg-cream-50 text-espresso-950">
                      <CheckGlyph />
                    </span>
                  ) : null}
                </button>
              );
            })}
          </div>
          <p className="muted text-xs">
            {de.userAvatarSelected}: {avatarId === null ? de.userAvatarRandomShort : avatarLabel(avatarId)}
          </p>
        </fieldset>
        {error ? <div className="text-sm text-rust-400" role="alert">{error}</div> : null}
      </form>
    </Modal>
  );
}

function TopupDialog({
  user,
  onClose,
  onSaved,
}: {
  user: UserWithBalance;
  onClose: () => void;
  onSaved: () => void;
}) {
  const [amount, setAmount] = useState("");
  const [note, setNote] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const writeId = useWriteId();

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    const money = parseMoneyInput(amount);
    if (!money.valid) {
      setError(de.invalidAmount);
      return;
    }
    if (money.cents <= 0) {
      setError(money.cents === 0 ? de.amountPositive : de.invalidAmount);
      return;
    }
    setBusy(true);
    setError("");
    try {
      await api.topup(user.id, money.cents, note.trim(), writeId.id);
      writeId.renew();
      onSaved();
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      title={de.topupTitle}
      onClose={onClose}
      footer={
        <>
          <button className="btn" type="button" onClick={onClose} disabled={busy}>
            {de.cancel}
          </button>
          <button className="btn btn-primary" type="submit" form="topup-form" disabled={busy}>
            {busy ? de.saving : de.save}
          </button>
        </>
      }
    >
      <form id="topup-form" className="space-y-4" onSubmit={submit}>
        <div className="muted text-sm">
          {user.name} · {de.balance} {formatEuro(user.balance_cents)}
        </div>
        <p className="text-sm text-cream-100/80">{de.topupHint}</p>
        <label className="block space-y-1.5">
          <span className="label">{de.topupAmount}</span>
          <MoneyInput value={amount} onChange={setAmount} autoFocus />
          <MoneyPreview value={amount} />
        </label>
        <label className="block space-y-1.5">
          <span className="label">{de.note} ({de.optional})</span>
          <input className="input" value={note} onChange={(event) => setNote(event.target.value)} maxLength={200} />
        </label>
        {error ? <div className="text-sm text-rust-400" role="alert">{error}</div> : null}
      </form>
    </Modal>
  );
}

function AdjustDialog({
  user,
  onClose,
  onSaved,
}: {
  user: UserWithBalance;
  onClose: () => void;
  onSaved: () => void;
}) {
  const [amount, setAmount] = useState("");
  const [note, setNote] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const writeId = useWriteId();

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    const money = parseMoneyInput(amount);
    if (!money.valid) {
      setError(de.invalidAmount);
      return;
    }
    if (money.cents === 0) {
      setError(de.amountNonZero);
      return;
    }
    if (note.trim().length < 3) {
      setError(de.noteRequired);
      return;
    }
    setBusy(true);
    setError("");
    try {
      await api.adjust(user.id, money.cents, note.trim(), writeId.id);
      writeId.renew();
      onSaved();
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      title={de.adjustmentTitle}
      onClose={onClose}
      footer={
        <>
          <button className="btn" type="button" onClick={onClose} disabled={busy}>
            {de.cancel}
          </button>
          <button className="btn btn-primary" type="submit" form="adjust-form" disabled={busy}>
            {busy ? de.saving : de.save}
          </button>
        </>
      }
    >
      <form id="adjust-form" className="space-y-4" onSubmit={submit}>
        <div className="muted text-sm">
          {user.name} · {de.balance} {formatEuro(user.balance_cents)}
        </div>
        <p className="text-sm text-cream-100/80">{de.adjustmentHint}</p>
        <label className="block space-y-1.5">
          <span className="label">{de.adjustmentAmount}</span>
          <MoneyInput value={amount} onChange={setAmount} autoFocus />
          <MoneyPreview value={amount} allowNegative />
        </label>
        <label className="block space-y-1.5">
          <span className="label">{de.note}</span>
          <input
            className="input"
            value={note}
            onChange={(event) => setNote(event.target.value)}
            maxLength={200}
            required
          />
        </label>
        {error ? <div className="text-sm text-rust-400" role="alert">{error}</div> : null}
      </form>
    </Modal>
  );
}

function HistoryDialog({ user, onClose }: { user: UserWithBalance; onClose: () => void }) {
  const history = useAsync(() => api.userTransactions(user.id, 200), [user.id]);
  return (
    <Modal open wide title={`${de.historyTitle} · ${user.name}`} onClose={onClose}>
      <div className="mb-3 flex flex-wrap items-baseline justify-between gap-2">
        <div className="muted text-sm">
          {de.balance}: <span className={`mono font-semibold ${user.balance_cents < 0 ? "text-rust-400" : "text-sage-400"}`}>{formatEuro(user.balance_cents)}</span>
        </div>
        <div className="muted text-xs">{de.createdAt}: {formatDate(user.created_at)}</div>
      </div>
      {history.error ? (
        <Alert tone="bad" action={<button className="btn" onClick={history.reload} type="button">{de.retry}</button>}>
          {history.error}
        </Alert>
      ) : null}
      {history.loading && !history.data ? <div className="muted">{de.loading}</div> : null}
      {history.data ? <TransactionTable events={history.data} /> : null}
    </Modal>
  );
}

function ArchiveDialog({
  user,
  onClose,
  onSaved,
}: {
  user: UserWithBalance;
  onClose: () => void;
  onSaved: () => void;
}) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const writeId = useWriteId();

  async function archive() {
    setBusy(true);
    setError("");
    try {
      await api.archiveUser(user.id, writeId.id);
      writeId.renew();
      onSaved();
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  }

  return (
    <ConfirmDialog
      open
      title={de.archiveUserTitle}
      body={
        <>
          <p>{de.archiveUserBody}</p>
          <p className="font-medium text-cream-50">{user.name}</p>
          {error ? <p className="text-rust-400" role="alert">{error}</p> : null}
        </>
      }
      confirmLabel={de.archive}
      busy={busy}
      onConfirm={archive}
      onClose={onClose}
    />
  );
}

/** Neutral placeholder for the "initials only" tile: never previews the typed name. */
function InitialsGlyph({ size = 32 }: { size?: number }) {
  return (
    <span
      className="inline-flex shrink-0 items-center justify-center rounded-full bg-espresso-900 text-cream-500"
      style={{ width: size, height: size }}
      aria-hidden="true"
    >
      <svg
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        strokeWidth={1.8}
        strokeLinecap="round"
        className="h-4 w-4"
      >
        <circle cx="12" cy="8" r="3.2" />
        <path d="M5 19.5c1.2-3.4 3.9-5.2 7-5.2s5.8 1.8 7 5.2" />
      </svg>
    </span>
  );
}

/** Non-colour selection cue for the picker tiles. */
function CheckGlyph() {
  return (
    <svg
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth={2.2}
      strokeLinecap="round"
      strokeLinejoin="round"
      className="h-2.5 w-2.5"
      aria-hidden="true"
    >
      <path d="M3.5 8.5 6.5 11.5 12.5 5" />
    </svg>
  );
}
