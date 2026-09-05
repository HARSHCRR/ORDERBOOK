/**
 * OrderBook -- asks on top (worst price at the top, best just above the spread
 * row), bids below (best first). That is the conventional exchange ladder:
 * prices increase as you go up, and the spread sits in the middle.
 *
 * The depth bar behind each row is scaled to the largest quantity currently
 * visible on EITHER side, so the two halves stay comparable at a glance.
 */

import { Panel } from "./Panel";
import type { Book, BookLevel } from "../types";

function Row({
  level,
  max,
  side,
}: {
  level: BookLevel;
  max: number;
  side: "bid" | "ask";
}) {
  const width = max > 0 ? (level.quantity / max) * 100 : 0;

  return (
    <div className="relative grid grid-cols-2 px-4 py-[3px] text-[13px]">
      <div
        className={`absolute inset-y-0 right-0 ${side === "bid" ? "bg-emerald-500/10" : "bg-rose-500/10"}`}
        style={{ width: `${width}%` }}
      />
      <span
        className={`relative font-medium ${side === "bid" ? "text-emerald-400" : "text-rose-400"}`}
      >
        {level.price.toFixed(2)}
      </span>
      <span className="relative text-right text-slate-300">{level.quantity}</span>
    </div>
  );
}

function Header() {
  return (
    <div className="grid grid-cols-2 px-4 pb-1 text-[10px] uppercase tracking-wider text-slate-600">
      <span>Price</span>
      <span className="text-right">Quantity</span>
    </div>
  );
}

function Empty({ label }: { label: string }) {
  return <div className="px-4 py-3 text-xs italic text-slate-600">{label}</div>;
}

export function OrderBook({ book }: { book: Book }) {
  const max = Math.max(
    1,
    ...book.bids.map((l) => l.quantity),
    ...book.asks.map((l) => l.quantity),
  );

  // Asks arrive best (lowest) first; reverse so the best ask sits closest to
  // the spread row in the middle.
  const asks = [...book.asks].reverse();

  return (
    <Panel
      title="Order Book"
      right={
        <div className="flex gap-4 text-[11px]">
          <span className="text-slate-500">
            Bid <span className="font-semibold text-emerald-400">{book.best_bid ?? "—"}</span>
          </span>
          <span className="text-slate-500">
            Ask <span className="font-semibold text-rose-400">{book.best_ask ?? "—"}</span>
          </span>
        </div>
      }
    >
      <div className="flex h-full flex-col">
        <div className="pt-2">
          <Header />
        </div>

        <div className="flex flex-1 flex-col justify-end overflow-y-auto">
          {asks.length ? (
            asks.map((level) => (
              <Row key={`a${level.price}`} level={level} max={max} side="ask" />
            ))
          ) : (
            <Empty label="no asks" />
          )}
        </div>

        <div className="my-1 flex items-center gap-3 border-y border-slate-800 bg-slate-950/60 px-4 py-2">
          <span className="text-[10px] uppercase tracking-[0.15em] text-slate-500">Spread</span>
          <span className="text-sm font-semibold text-slate-200">
            {book.spread !== null ? book.spread.toFixed(2) : "—"}
          </span>
          {book.spread === null && (
            <span className="text-[10px] text-slate-600">(one side empty)</span>
          )}
        </div>

        <div className="flex-1 overflow-y-auto">
          {book.bids.length ? (
            book.bids.map((level) => (
              <Row key={`b${level.price}`} level={level} max={max} side="bid" />
            ))
          ) : (
            <Empty label="no bids" />
          )}
        </div>
      </div>
    </Panel>
  );
}
