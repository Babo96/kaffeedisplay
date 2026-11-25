import { api } from "../api/endpoints";
import type { LongTermMonth, LongTermProductStat, LongTermStats, LongTermUserStat } from "../api/types";
import { Alert } from "../components/Alert";
import { DataTable } from "../components/DataTable";
import { PageHeader } from "../components/PageHeader";
import { StatCard } from "../components/StatCard";
import { de } from "../i18n/de";
import { formatDate, formatEuro, formatInteger } from "../lib/format";
import { timeQualityState } from "../lib/report";
import { useAsync } from "../lib/useAsync";

const monthFormat = new Intl.DateTimeFormat("de-DE", {
  month: "long",
  year: "numeric",
  timeZone: "UTC",
});

function monthLabel(month: LongTermMonth): string {
  return monthFormat.format(new Date(Date.UTC(month.year, month.month - 1, 1)));
}

export function LongTermPage() {
  const { data, error, loading, reload } = useAsync<LongTermStats>(
    () => api.longTermStats(),
    [],
  );

  const header = (
    <PageHeader
      title={de.longTermTitle}
      subtitle={de.longTermHint}
      actions={
        <button className="btn" onClick={reload} type="button" disabled={loading}>
          {loading ? de.loading : de.refresh}
        </button>
      }
    />
  );

  if (error) {
    return (
      <div className="space-y-4">
        {header}
        <Alert
          tone="bad"
          action={
            <button className="btn" onClick={reload} type="button">
              {de.retry}
            </button>
          }
        >
          {error}
        </Alert>
      </div>
    );
  }

  if (!data) {
    return (
      <div className="space-y-4">
        {header}
        <div className="muted">{loading ? de.loading : de.noData}</div>
      </div>
    );
  }

  const totalItems = data.member_items + data.guest_items;
  const quality = timeQualityState(data.time_quality?.trusted);
  const range =
    data.first_sale_at > 0 && data.last_sale_at > 0
      ? `${formatDate(data.first_sale_at)} – ${formatDate(data.last_sale_at)}`
      : de.longTermNoSales;

  return (
    <div className="space-y-5">
      {header}

      {totalItems === 0 ? (
        <div className="card p-8 text-center muted">{de.longTermEmpty}</div>
      ) : (
        <>
          <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
            <StatCard
              label={de.longTermMemberItems}
              value={formatInteger(data.member_items)}
              hint={formatEuro(data.member_spend_cents)}
              tone="good"
            />
            <StatCard
              label={de.longTermGuestItems}
              value={formatInteger(data.guest_items)}
              hint={formatEuro(data.guest_spend_cents)}
            />
            <StatCard label={de.longTermTotalItems} value={formatInteger(totalItems)} />
            <StatCard
              label={de.longTermEstimated}
              value={formatInteger(data.estimated_items)}
              tone={data.estimated_items > 0 ? "warn" : "neutral"}
              hint={
                data.estimated_items > 0
                  ? de.longTermEstimatedHint
                  : de.longTermEstimatedNone
              }
            />
          </div>

          <div className="card flex flex-wrap items-center justify-between gap-2 p-3 text-sm">
            <div className="muted">
              {de.longTermRange}: {range}
            </div>
            {quality === "trusted" ? (
              <span className="text-sage-400">{de.trustedTime}</span>
            ) : quality === "untrusted" ? (
              <span className="text-amber-400">{de.untrustedTime}</span>
            ) : (
              <span className="muted">{de.timeQualityUnknown}</span>
            )}
          </div>

          <section className="card p-4 sm:p-5">
            <h2 className="font-medium">{de.longTermPerUser}</h2>
            <div className="mt-3">
              <DataTable<LongTermUserStat>
                columns={[
                  {
                    key: "name",
                    label: de.name,
                    render: (row) => <span>{row.name || row.user_id}</span>,
                  },
                  {
                    key: "items",
                    label: de.longTermCups,
                    align: "right",
                    render: (row) => formatInteger(row.items),
                  },
                  {
                    key: "spend_cents",
                    label: de.longTermSpend,
                    align: "right",
                    render: (row) => formatEuro(row.spend_cents),
                  },
                ]}
                rows={data.users}
                rowKey={(row) => row.user_id}
                empty={de.longTermNoSales}
              />
            </div>
          </section>

          <section className="card p-4 sm:p-5">
            <h2 className="font-medium">{de.longTermPerProduct}</h2>
            <div className="mt-3">
              <DataTable<LongTermProductStat>
                columns={[
                  {
                    key: "name",
                    label: de.products,
                    render: (row) => (
                      <span>
                        {row.name || row.product_id}
                        {row.category ? <span className="muted"> · {row.category}</span> : null}
                      </span>
                    ),
                  },
                  {
                    key: "items",
                    label: de.longTermCups,
                    align: "right",
                    render: (row) => formatInteger(row.items),
                  },
                  {
                    key: "spend_cents",
                    label: de.longTermSpend,
                    align: "right",
                    render: (row) => formatEuro(row.spend_cents),
                  },
                ]}
                rows={data.products}
                rowKey={(row) => row.product_id}
                empty={de.longTermNoSales}
              />
            </div>
          </section>

          <section className="card p-4 sm:p-5">
            <h2 className="font-medium">{de.longTermPerMonth}</h2>
            <div className="mt-3">
              <DataTable<LongTermMonth>
                columns={[
                  {
                    key: "month",
                    label: de.month,
                    render: (row) => monthLabel(row),
                  },
                  {
                    key: "member_items",
                    label: de.longTermMemberCups,
                    align: "right",
                    render: (row) => formatInteger(row.member_items),
                  },
                  {
                    key: "guest_items",
                    label: de.longTermGuestCups,
                    align: "right",
                    render: (row) => formatInteger(row.guest_items),
                  },
                  {
                    key: "member_spend_cents",
                    label: de.longTermMemberSpend,
                    align: "right",
                    hideBelow: "md",
                    render: (row) => formatEuro(row.member_spend_cents),
                  },
                  {
                    key: "guest_spend_cents",
                    label: de.longTermGuestSpend,
                    align: "right",
                    hideBelow: "lg",
                    render: (row) => formatEuro(row.guest_spend_cents),
                  },
                ]}
                rows={data.months}
                rowKey={(row) => `${row.year}-${row.month}`}
                empty={de.longTermNoSales}
              />
            </div>
          </section>
        </>
      )}
    </div>
  );
}
