/**
 * TradeHistory -- the tape, newest first.
 *
 * The C++ engine records only (buyId, sellId, price, quantity); it has no
 * clock. Rather than invent a timestamp the engine never produced, each row is
 * keyed by the engine's own execution sequence number, which is the honest
 * ordering signal. The "arrived" time is stamped by the browser when the event
 * landed, and is labelled as such.
 */

import { Panel } from "./Panel";
import type { Trade } from "../types";

function counterparty(trade: Trade): string {
  // -1 means the aggressor was a market order, which the engine never assigns
  // a resting id to.
  if (trade.buy_id === -1) return `MKT BUY ▸ #${trade.sell_id}`;
  if (trade.sell_id === -1) return `MKT SELL ▸ #${trade.buy_id}`;
  return `#${trade.buy_id} ▸ #${trade.sell_id}`;
}

export function TradeHistory({ trades }: { trades: Trade[] }) {
  return (
    <Panel
      title="Recent Trades"
      right={<span className="text-[11px] text-slate-600">{trades.length} shown</span>}
    >
      <div className="flex h-full flex-col">
        <div className="grid grid-cols-[1fr_1fr_1.4fr] px-4 pb-1 pt-2 text-[10px] uppercase tracking-wider text-slate-600">
          <span>Price</span>
          <span className="text-right">Qty</span>
          <span className="text-right">Buy ▸ Sell</span>
        </div>

        <div className="flex-1 overflow-y-auto">
          {trades.length === 0 && (
            <p className="px-4 py-3 text-xs italic text-slate-600">
              No trades yet — place crossing orders to fill.
            </p>
          )}

          {trades.map((trade) => (
            <div
              key={trade.seq}
              className="grid grid-cols-[1fr_1fr_1.4fr] px-4 py-[3px] text-[13px] odd:bg-slate-950/30"
            >
              <span className="font-medium text-slate-200">{trade.price.toFixed(2)}</span>
              <span className="text-right text-slate-300">{trade.quantity}</span>
              <span className="text-right text-[11px] text-slate-500">
                {counterparty(trade)}
              </span>
            </div>
          ))}
        </div>
      </div>
    </Panel>
  );
}
