/**
 * OrderForm -- the only place in the UI that writes to the engine.
 *
 * It POSTs to /orders and hands the execution report back to App, which shows
 * it in the ExecutionReport panel. It does NOT touch the book or trade state:
 * those arrive over the WebSocket a moment later, driven by the same engine
 * command. One writer, one source of truth.
 */

import { useState } from "react";
import type { FormEvent } from "react";
import { Panel } from "./Panel";
import { ApiError, api } from "../api";
import type { ExecutionReport, OrderType, Side } from "../types";

export function OrderForm({
  onExecuted,
  disabled,
}: {
  onExecuted: (report: ExecutionReport) => void;
  disabled: boolean;
}) {
  const [side, setSide] = useState<Side>("BUY");
  const [type, setType] = useState<OrderType>("LIMIT");
  const [price, setPrice] = useState("100");
  const [quantity, setQuantity] = useState("10");
  const [error, setError] = useState<string | null>(null);
  const [pending, setPending] = useState(false);

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError(null);

    const qty = Number(quantity);
    if (!Number.isInteger(qty) || qty <= 0) {
      setError("Quantity must be a whole number greater than 0.");
      return;
    }

    // The engine works in integer prices (see engine/OrderBook.hpp), so the
    // UI refuses fractional input rather than silently rounding it.
    let px: number | undefined;
    if (type === "LIMIT") {
      px = Number(price);
      if (!Number.isInteger(px) || px <= 0) {
        setError("Price must be a whole number greater than 0.");
        return;
      }
    }

    setPending(true);
    try {
      const report = await api.placeOrder({ side, type, price: px, quantity: qty });
      onExecuted(report);
    } catch (err) {
      setError(err instanceof ApiError ? err.message : "Could not reach the exchange.");
    } finally {
      setPending(false);
    }
  }

  const buying = side === "BUY";

  return (
    <Panel title="Order Entry">
      <form onSubmit={submit} className="flex flex-col gap-3 p-4">
        <div className="grid grid-cols-2 gap-2">
          {(["BUY", "SELL"] as const).map((option) => (
            <button
              key={option}
              type="button"
              onClick={() => setSide(option)}
              className={`rounded-md border py-2 text-sm font-semibold transition ${
                side === option
                  ? option === "BUY"
                    ? "border-emerald-500 bg-emerald-500/15 text-emerald-300"
                    : "border-rose-500 bg-rose-500/15 text-rose-300"
                  : "border-slate-800 text-slate-500 hover:border-slate-700 hover:text-slate-300"
              }`}
            >
              {option}
            </button>
          ))}
        </div>

        <div className="grid grid-cols-2 gap-2">
          {(["LIMIT", "MARKET"] as const).map((option) => (
            <button
              key={option}
              type="button"
              onClick={() => setType(option)}
              className={`rounded-md border py-1.5 text-xs font-medium tracking-wide transition ${
                type === option
                  ? "border-slate-600 bg-slate-800 text-slate-100"
                  : "border-slate-800 text-slate-500 hover:border-slate-700 hover:text-slate-300"
              }`}
            >
              {option}
            </button>
          ))}
        </div>

        <label className="flex flex-col gap-1">
          <span className="text-[10px] uppercase tracking-wider text-slate-500">Price</span>
          <input
            type="number"
            min={1}
            step={1}
            value={type === "MARKET" ? "" : price}
            onChange={(e) => setPrice(e.target.value)}
            disabled={type === "MARKET"}
            placeholder={type === "MARKET" ? "Market — best available" : "100"}
            className="rounded-md border border-slate-800 bg-slate-950 px-3 py-2 text-sm text-slate-100 outline-none placeholder:text-slate-700 focus:border-slate-600 disabled:cursor-not-allowed disabled:text-slate-700"
          />
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-[10px] uppercase tracking-wider text-slate-500">Quantity</span>
          <input
            type="number"
            min={1}
            step={1}
            value={quantity}
            onChange={(e) => setQuantity(e.target.value)}
            className="rounded-md border border-slate-800 bg-slate-950 px-3 py-2 text-sm text-slate-100 outline-none focus:border-slate-600"
          />
        </label>

        {error && (
          <p className="rounded-md border border-rose-900 bg-rose-950/50 px-3 py-2 text-xs text-rose-300">
            {error}
          </p>
        )}

        <button
          type="submit"
          disabled={pending || disabled}
          className={`rounded-md py-2.5 text-sm font-semibold text-slate-950 transition disabled:cursor-not-allowed disabled:opacity-40 ${
            buying
              ? "bg-emerald-500 hover:bg-emerald-400"
              : "bg-rose-500 hover:bg-rose-400"
          }`}
        >
          {pending ? "Sending…" : `Place ${type} ${side}`}
        </button>
      </form>
    </Panel>
  );
}
