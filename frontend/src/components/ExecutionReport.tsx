/**
 * ExecutionReport -- the result of the last order this browser submitted.
 *
 * Every field shown here comes from the POST /orders response, which the C++
 * engine produced. Notably `order_id` is real even when the order filled
 * completely: the engine's buy()/sell() return -1 in that case, so the bridge
 * reads the id the engine was about to assign before calling it.
 *
 * If the order is still resting, a Cancel button routes to
 * DELETE /orders/{id} -> the engine's iterator-based cancelOrder().
 */

import { useState } from "react";
import { Panel } from "./Panel";
import { ApiError, api } from "../api";
import type { ExecutionReport as Report } from "../types";

const STATUS_STYLE: Record<string, string> = {
  FILLED: "border-emerald-700 bg-emerald-950/60 text-emerald-300",
  PARTIALLY_FILLED: "border-amber-700 bg-amber-950/60 text-amber-300",
  RESTING: "border-sky-700 bg-sky-950/60 text-sky-300",
  REJECTED: "border-rose-800 bg-rose-950/60 text-rose-300",
  CANCELLED: "border-slate-700 bg-slate-900 text-slate-400",
};

function Field({ label, value }: { label: string; value: string | number }) {
  return (
    <div>
      <p className="text-[10px] uppercase tracking-wider text-slate-500">{label}</p>
      <p className="text-sm font-medium text-slate-200">{value}</p>
    </div>
  );
}

export function ExecutionReport({ report }: { report: Report | null }) {
  const [cancelState, setCancelState] = useState<string | null>(null);
  const [cancelError, setCancelError] = useState<string | null>(null);

  if (!report) {
    return (
      <Panel title="Execution Report">
        <p className="p-4 text-xs italic text-slate-600">
          Submit an order to see its execution result.
        </p>
      </Panel>
    );
  }

  // Reset the cancel banner whenever a newer order arrives.
  const key = report.order_id;

  async function cancel() {
    setCancelError(null);
    try {
      await api.cancelOrder(key);
      setCancelState("CANCELLED");
    } catch (err) {
      setCancelError(err instanceof ApiError ? err.message : "Cancel failed.");
    }
  }

  const status = cancelState ?? report.status;
  const canCancel = report.resting && cancelState === null;

  return (
    <Panel
      title="Execution Report"
      right={
        <span
          className={`rounded border px-2 py-0.5 text-[10px] font-semibold tracking-wider ${
            STATUS_STYLE[status] ?? STATUS_STYLE.RESTING
          }`}
        >
          {status.replace("_", " ")}
        </span>
      }
    >
      <div key={key} className="flex flex-col gap-3 p-4">
        <div className="grid grid-cols-3 gap-3">
          <Field label="Order ID" value={`#${report.order_id}`} />
          <Field label="Side / Type" value={`${report.side} ${report.type}`} />
          <Field label="Price" value={report.price ?? "MARKET"} />
          <Field label="Requested" value={report.requested_quantity} />
          <Field label="Filled" value={report.filled_quantity} />
          <Field label="Remaining" value={report.remaining_quantity} />
        </div>

        {report.trades.length > 0 && (
          <div className="rounded-md border border-slate-800 bg-slate-950/50 p-2">
            <p className="mb-1 text-[10px] uppercase tracking-wider text-slate-500">
              Generated {report.trades.length} trade{report.trades.length > 1 ? "s" : ""}
            </p>
            {report.trades.map((trade) => (
              <p key={trade.seq} className="text-xs text-slate-300">
                {trade.quantity} @ {trade.price.toFixed(2)}
              </p>
            ))}
          </div>
        )}

        {report.status === "REJECTED" && (
          <p className="text-xs text-rose-300">
            Market order found no liquidity — nothing to match against.
          </p>
        )}

        {canCancel && (
          <button
            onClick={cancel}
            className="rounded-md border border-slate-700 py-1.5 text-xs font-medium text-slate-300 transition hover:border-slate-500 hover:text-slate-100"
          >
            Cancel order #{report.order_id}
          </button>
        )}

        {cancelError && <p className="text-xs text-rose-300">{cancelError}</p>}
      </div>
    </Panel>
  );
}
