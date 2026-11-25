import { useMemo, useState } from "react";
import { newRequestId } from "../api/client";
import { api } from "../api/endpoints";
import type { Product, ProductInput } from "../api/types";
import { Alert } from "../components/Alert";
import { ConfirmDialog } from "../components/ConfirmDialog";
import { Modal } from "../components/Modal";
import { MoneyInput, MoneyPreview } from "../components/MoneyInput";
import { PageHeader } from "../components/PageHeader";
import { ProductIcon, productIcons } from "../components/ProductIcon";
import { de } from "../i18n/de";
import { errorText } from "../lib/errors";
import { formatEuro, inputFromCents, parseMoneyInput } from "../lib/format";
import { toProductInput } from "../lib/productInput";
import { useAsync } from "../lib/useAsync";
import { useWriteId } from "../lib/useWriteId";

const colors = ["#D4A373", "#B5835A", "#9B6B43", "#8FBC8F", "#7FA7C9", "#E07A5F", "#C9A0DC", "#F2CC8F"];

const iconLabels: Record<string, string> = {
  cup: de.iconCup,
  espresso: de.iconEspresso,
  cocoa: de.iconCocoa,
  tea: de.iconTea,
  water: de.iconWater,
  soda: de.iconSoda,
  beer: de.iconBeer,
  snack: de.iconSnack,
};

type Dialog =
  | { kind: "create" }
  | { kind: "edit"; product: Product }
  | { kind: "archive"; product: Product }
  | { kind: "delete"; product: Product }
  | null;

export function ProductsPage() {
  const [search, setSearch] = useState("");
  const [showArchived, setShowArchived] = useState(false);
  const [dialog, setDialog] = useState<Dialog>(null);
  const [notice, setNotice] = useState("");
  const [noticeTone, setNoticeTone] = useState<"good" | "bad">("good");
  const [moving, setMoving] = useState("");
  const writeId = useWriteId();
  const products = useAsync<Product[]>(() => api.products(true), []);

  const filtered = useMemo(() => {
    const query = search.trim().toLowerCase();
    return (products.data ?? [])
      .filter((product) => {
        if (!showArchived && !product.active) return false;
        if (!query) return true;
        return (
          (product.name ?? "").toLowerCase().includes(query) ||
          (product.category ?? "").toLowerCase().includes(query)
        );
      })
      .sort((a, b) => a.sort - b.sort || a.name.localeCompare(b.name, "de"));
  }, [products.data, search, showArchived]);

  function saved(message: string) {
    setNoticeTone("good");
    setNotice(message);
    setDialog(null);
    products.reload();
  }

  async function move(product: Product, direction: -1 | 1) {
    const index = filtered.findIndex((item) => item.id === product.id);
    const neighbor = filtered[index + direction];
    if (!neighbor) return;
    setMoving(product.id);
    setNotice("");
    try {
      await Promise.all([
        api.updateProduct(
          product.id,
          toProductInput(product, { sort: neighbor.sort }),
          newRequestId(),
        ),
        api.updateProduct(
          neighbor.id,
          toProductInput(neighbor, { sort: product.sort }),
          newRequestId(),
        ),
      ]);
      products.reload();
    } catch (err) {
      setNoticeTone("bad");
      setNotice(errorText(err));
    } finally {
      setMoving("");
    }
  }

  async function setActive(product: Product, active: boolean) {
    setMoving(product.id);
    setNotice("");
    try {
      await api.updateProduct(product.id, toProductInput(product, { active }), writeId.id);
      writeId.renew();
      setNoticeTone("good");
      setNotice(active ? de.productRestored : de.productArchived);
      products.reload();
    } catch (err) {
      setNoticeTone("bad");
      setNotice(errorText(err));
    } finally {
      setMoving("");
    }
  }

  return (
    <div className="space-y-5">
      <PageHeader
        title={de.products}
        subtitle={`${(products.data ?? []).filter((product) => product.active).length} ${de.active}`}
        actions={
          <button className="btn btn-primary" type="button" onClick={() => setDialog({ kind: "create" })}>
            {de.addProduct}
          </button>
        }
      />

      {notice ? <Alert tone={noticeTone}>{notice}</Alert> : null}
      {products.error ? (
        <Alert tone="bad" action={<button className="btn" onClick={products.reload} type="button">{de.retry}</button>}>
          {products.error}
        </Alert>
      ) : null}

      <div className="flex flex-col gap-2 sm:flex-row sm:items-center">
        <label className="flex-1">
          <span className="sr-only">{de.searchProducts}</span>
          <input
            className="input"
            value={search}
            onChange={(event) => setSearch(event.target.value)}
            placeholder={`${de.searchProducts} …`}
            type="search"
          />
        </label>
        <label className="flex min-h-11 items-center gap-2 rounded-xl border border-cream-100/10 px-3 text-sm">
          <input
            type="checkbox"
            className="h-4 w-4 accent-amber-400"
            checked={showArchived}
            onChange={(event) => setShowArchived(event.target.checked)}
          />
          {de.showArchived}
        </label>
      </div>

      {products.loading && !products.data ? <div className="muted">{de.loading}</div> : null}
      {!products.loading && filtered.length === 0 ? (
        <div className="card p-8 text-center">
          <div className="muted">{products.data && products.data.length > 0 ? de.noResults : de.noProducts}</div>
        </div>
      ) : null}

      <ul className="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
        {filtered.map((product, index) => (
          <li key={product.id} className={`card p-4 ${product.active ? "" : "opacity-70"}`}>
            <div className="flex items-start gap-3">
              <span
                className="flex h-12 w-12 shrink-0 items-center justify-center rounded-2xl text-espresso-950"
                style={{ backgroundColor: product.color }}
                aria-hidden="true"
              >
                <ProductIcon name={product.icon} className="h-7 w-7" />
              </span>
              <div className="min-w-0 flex-1">
                <div className="flex items-start justify-between gap-2">
                  <div className="min-w-0">
                    <div className="truncate font-medium">{product.name}</div>
                    <div className="muted text-xs">{product.category}</div>
                  </div>
                  <div className="text-right">
                    <div className="mono font-semibold">{formatEuro(product.price_cents)}</div>
                    <div className="muted text-[11px]">
                      {product.guest_price_cents != null
                        ? `${de.productGuestPriceBadge}: ${formatEuro(product.guest_price_cents)}`
                        : `${de.productGuestPriceBadge}: ${de.productGuestPriceInherit}`}
                    </div>
                    <div className="muted text-[11px]">
                      {de.sort} {product.sort} · {product.active ? de.active : de.inactive}
                    </div>
                  </div>
                </div>
              </div>
            </div>
            <div className="mt-3 flex flex-wrap gap-1.5">
              <button className="btn min-h-9 px-2.5 py-1 text-xs" type="button" onClick={() => setDialog({ kind: "edit", product })}>
                {de.edit}
              </button>
              <button
                className="btn min-h-9 px-2.5 py-1 text-xs"
                type="button"
                onClick={() => move(product, -1)}
                disabled={index === 0 || moving === product.id}
                aria-label={de.moveUp}
              >
                ↑
              </button>
              <button
                className="btn min-h-9 px-2.5 py-1 text-xs"
                type="button"
                onClick={() => move(product, 1)}
                disabled={index === filtered.length - 1 || moving === product.id}
                aria-label={de.moveDown}
              >
                ↓
              </button>
              {product.active ? (
                <button className="btn min-h-9 px-2.5 py-1 text-xs" type="button" onClick={() => setDialog({ kind: "archive", product })}>
                  {de.archive}
                </button>
              ) : (
                <button className="btn min-h-9 px-2.5 py-1 text-xs" type="button" onClick={() => setActive(product, true)} disabled={moving === product.id}>
                  {de.restore}
                </button>
              )}
              <button className="btn btn-danger min-h-9 px-2.5 py-1 text-xs" type="button" onClick={() => setDialog({ kind: "delete", product })}>
                {de.delete}
              </button>
            </div>
          </li>
        ))}
      </ul>

      {dialog?.kind === "create" ? (
        <ProductFormDialog
          nextSort={(products.data ?? []).reduce((max, product) => Math.max(max, product.sort), -1) + 1}
          onClose={() => setDialog(null)}
          onSaved={() => saved(de.productCreated)}
        />
      ) : null}
      {dialog?.kind === "edit" ? (
        <ProductFormDialog
          product={dialog.product}
          nextSort={dialog.product.sort}
          onClose={() => setDialog(null)}
          onSaved={() => saved(de.productUpdated)}
        />
      ) : null}
      {dialog?.kind === "archive" ? (
        <ConfirmProductDialog
          title={de.archiveProductTitle}
          body={de.archiveProductBody}
          product={dialog.product}
          confirmLabel={de.archive}
          action={(requestId) =>
            api.updateProduct(
              dialog.product.id,
              toProductInput(dialog.product, { active: false }),
              requestId,
            )
          }
          onClose={() => setDialog(null)}
          onSaved={() => saved(de.productArchived)}
        />
      ) : null}
      {dialog?.kind === "delete" ? (
        <ConfirmProductDialog
          title={de.deleteProductTitle}
          body={de.deleteProductBody}
          product={dialog.product}
          confirmLabel={de.deleteProductConfirm}
          action={(requestId) => api.deleteProduct(dialog.product.id, requestId)}
          onClose={() => setDialog(null)}
          onSaved={() => saved(de.productDeleted)}
        />
      ) : null}
    </div>
  );
}

function ProductFormDialog({
  product,
  nextSort,
  onClose,
  onSaved,
}: {
  product?: Product;
  nextSort: number;
  onClose: () => void;
  onSaved: () => void;
}) {
  const [name, setName] = useState(product?.name ?? "");
  const [category, setCategory] = useState(product?.category ?? "Kaffee");
  const [price, setPrice] = useState(product ? inputFromCents(product.price_cents) : "");
  const [guestPrice, setGuestPrice] = useState(
    product?.guest_price_cents != null ? inputFromCents(product.guest_price_cents) : "",
  );
  const [icon, setIcon] = useState(product?.icon ?? "cup");
  const [color, setColor] = useState(product?.color ?? colors[0]);
  const [sort, setSort] = useState(String(product?.sort ?? nextSort));
  const [active, setActive] = useState(product?.active ?? true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const writeId = useWriteId();

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    const money = parseMoneyInput(price);
    if (!name.trim() || !category.trim()) {
      setError(de.required);
      return;
    }
    if (!money.valid || money.cents < 0) {
      setError(de.invalidAmount);
      return;
    }
    const hasGuestPrice = guestPrice.trim() !== "";
    const guestMoney = hasGuestPrice ? parseMoneyInput(guestPrice) : null;
    if (guestMoney && (!guestMoney.valid || guestMoney.cents < 0)) {
      setError(de.invalidAmount);
      return;
    }
    const input: ProductInput = {
      name: name.trim(),
      category: category.trim(),
      price_cents: money.cents,
      guest_price_cents: guestMoney ? guestMoney.cents : null,
      icon,
      color,
      sort: Number.parseInt(sort, 10) || 0,
      active,
    };
    setBusy(true);
    setError("");
    try {
      if (product) {
        await api.updateProduct(product.id, input, writeId.id);
      } else {
        await api.createProduct(input, writeId.id);
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
      title={product ? de.editProductTitle : de.createProductTitle}
      onClose={onClose}
      footer={
        <>
          <button className="btn" type="button" onClick={onClose} disabled={busy}>
            {de.cancel}
          </button>
          <button className="btn btn-primary" type="submit" form="product-form" disabled={busy}>
            {busy ? de.saving : de.save}
          </button>
        </>
      }
    >
      <form id="product-form" className="space-y-4" onSubmit={submit}>
        <div className="grid gap-3 sm:grid-cols-2">
          <label className="block space-y-1.5">
            <span className="label">{de.productName}</span>
            <input
              className="input"
              value={name}
              onChange={(event) => setName(event.target.value)}
              maxLength={40}
              required
              autoFocus
            />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.productCategory}</span>
            <input
              className="input"
              value={category}
              onChange={(event) => setCategory(event.target.value)}
              maxLength={40}
              list="category-options"
              required
            />
            <datalist id="category-options">
              {Array.from(new Set(["Kaffee", "Tee", "Kaltgetränke", "Snacks"])).map((option) => (
                <option key={option} value={option} />
              ))}
            </datalist>
          </label>
        </div>

        <div className="grid gap-3 sm:grid-cols-3">
          <label className="block space-y-1.5">
            <span className="label">{de.productPrice}</span>
            <MoneyInput value={price} onChange={setPrice} />
            <MoneyPreview value={price} />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.productGuestPrice}</span>
            <MoneyInput value={guestPrice} onChange={setGuestPrice} />
            <span className="muted block text-xs">{de.productGuestPriceHint}</span>
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.sort}</span>
            <input
              className="input mono"
              type="number"
              value={sort}
              onChange={(event) => setSort(event.target.value)}
            />
          </label>
        </div>

        <div className="space-y-2">
          <span className="label">{de.productIcon}</span>
          <div className="flex flex-wrap gap-1.5">
            {productIcons.map((iconName) => (
              <button
                key={iconName}
                type="button"
                className={`flex min-h-10 items-center gap-1.5 rounded-xl border px-2.5 py-1 text-xs ${
                  icon === iconName ? "border-amber-400 bg-amber-400/10 text-amber-400" : "border-cream-100/10"
                }`}
                onClick={() => setIcon(iconName)}
                title={iconLabels[iconName]}
              >
                <ProductIcon name={iconName} className="h-4 w-4" />
                <span>{iconLabels[iconName]}</span>
              </button>
            ))}
          </div>
        </div>

        <div className="space-y-2">
          <span className="label">{de.productColor}</span>
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
            <input
              type="color"
              className="h-9 w-12 rounded-lg border border-cream-100/20 bg-transparent"
              value={color}
              onChange={(event) => setColor(event.target.value)}
              aria-label={de.productColor}
            />
          </div>
        </div>

        <label className="flex min-h-11 items-center gap-2 text-sm">
          <input
            type="checkbox"
            className="h-4 w-4 accent-amber-400"
            checked={active}
            onChange={(event) => setActive(event.target.checked)}
          />
          {de.productActive}
        </label>

        {error ? <div className="text-sm text-rust-400" role="alert">{error}</div> : null}
      </form>
    </Modal>
  );
}

function ConfirmProductDialog({
  title,
  body,
  product,
  confirmLabel,
  action,
  onClose,
  onSaved,
}: {
  title: string;
  body: string;
  product: Product;
  confirmLabel: string;
  action: (requestId: string) => Promise<unknown>;
  onClose: () => void;
  onSaved: () => void;
}) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const writeId = useWriteId();

  async function run() {
    setBusy(true);
    setError("");
    try {
      await action(writeId.id);
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
      title={title}
      body={
        <>
          <p>{body}</p>
          <p className="font-medium text-cream-50">{product.name}</p>
          {error ? <p className="text-rust-400" role="alert">{error}</p> : null}
        </>
      }
      confirmLabel={confirmLabel}
      busy={busy}
      onConfirm={run}
      onClose={onClose}
    />
  );
}
