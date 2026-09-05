/**
 * Stats -- counters read straight off the engine (GET /stats, then pushed on
 * every STATS event). Nothing here is computed in the browser.
 */

import { Panel } from "./Panel";
import type { Stats as StatsType } from "../types";

function Tile({ label, value }: { label: string; value: string | number }) {
  return (
    <div className="rounded-md border border-slate-800 bg-slate-950/50 px-3 py-2.5">
      <p className="text-[10px] uppercase tracking-wider text-slate-500">{label}</p>
      <p className="mt-0.5 text-lg font-semibold text-slate-100">{value}</p>
    </div>
  );
}

export function Stats({ stats }: { stats: StatsType }) {
  return (
    <Panel title="Engine Statistics">
      <div className="grid grid-cols-2 gap-2 p-3">
        <Tile label="Orders Processed" value={stats.orders_processed.toLocaleString()} />
        <Tile label="Trades Executed" value={stats.trades_executed.toLocaleString()} />
        <Tile label="Active Orders" value={stats.active_orders.toLocaleString()} />
        <Tile label="Spread" value={stats.spread ?? "—"} />
      </div>
    </Panel>
  );
}
