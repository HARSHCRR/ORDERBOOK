/** Shared chrome for every card on the dashboard. Keeps the terminal look consistent. */
import type { ReactNode } from "react";

export function Panel({
  title,
  right,
  children,
  className = "",
}: {
  title: string;
  right?: ReactNode;
  children: ReactNode;
  className?: string;
}) {
  return (
    <section
      className={`flex flex-col rounded-lg border border-slate-800 bg-slate-900/40 ${className}`}
    >
      <header className="flex items-center justify-between border-b border-slate-800 px-4 py-2.5">
        <h2 className="text-[11px] font-semibold uppercase tracking-[0.14em] text-slate-400">
          {title}
        </h2>
        {right}
      </header>
      <div className="flex-1 overflow-hidden">{children}</div>
    </section>
  );
}
