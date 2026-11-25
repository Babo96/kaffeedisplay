import type { ReactNode } from "react";
import { NavLink } from "react-router-dom";
import { de } from "../i18n/de";

const nav = [
  { to: "/", label: de.dashboard, end: true },
  { to: "/users", label: de.users },
  { to: "/products", label: de.products },
  { to: "/expenses", label: de.expenses },
  { to: "/reports", label: de.reports },
  { to: "/longterm", label: de.longTerm },
  { to: "/cash", label: de.cash },
  { to: "/settings", label: de.settings },
  { to: "/system", label: de.system },
];

export function Shell({
  children,
  username,
  onLogout,
}: {
  children: ReactNode;
  username: string;
  onLogout: () => void;
}) {
  return (
    <div className="min-h-screen lg:grid lg:grid-cols-[250px_1fr]">
      <aside className="border-b border-cream-100/10 bg-espresso-900/70 lg:sticky lg:top-0 lg:h-screen lg:border-b-0 lg:border-r">
        <div className="flex items-center justify-between gap-3 px-4 py-3 lg:block lg:p-4">
          <div className="min-w-0">
            <div className="text-xl font-semibold tracking-tight">{de.app}</div>
            <div className="muted mt-1 truncate text-xs">{de.tagline}</div>
          </div>
          <div className="lg:hidden">
            <button
              className="btn min-h-9 px-3 py-1 text-sm"
              onClick={onLogout}
              type="button"
            >
              {de.signOut}
            </button>
          </div>
        </div>

        <div className="hidden px-4 pb-2 lg:block">
          <div className="rounded-xl border border-cream-100/10 px-3 py-2">
            <div className="label">{de.admin}</div>
            <div className="mt-0.5 truncate text-sm">{username}</div>
          </div>
        </div>

        <nav
          aria-label={de.app}
          className="flex gap-1.5 overflow-x-auto px-3 pb-2 lg:mt-4 lg:flex-col lg:overflow-visible lg:px-4"
        >
          {nav.map((item) => (
            <NavLink
              key={item.to}
              to={item.to}
              end={item.end}
              className={({ isActive }) =>
                `shrink-0 rounded-xl px-3 py-2 text-sm whitespace-nowrap transition ${
                  isActive
                    ? "bg-amber-400 font-semibold text-espresso-950"
                    : "text-cream-100/80 hover:bg-cream-100/10"
                }`
              }
            >
              {item.label}
            </NavLink>
          ))}
        </nav>

        <div className="hidden p-4 lg:block">
          <button className="btn w-full" onClick={onLogout} type="button">
            {de.signOut}
          </button>
        </div>
      </aside>

      <main className="mx-auto w-full max-w-6xl px-3 pt-4 pb-10 sm:px-6 sm:pt-6 lg:p-10">
        {children}
      </main>
    </div>
  );
}
