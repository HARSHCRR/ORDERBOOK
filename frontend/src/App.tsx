/**
 * App -- layout and the one piece of local state the dashboard owns.
 *
 * Everything shared (book, tape, stats) lives in useExchangeFeed and is pushed
 * by the server. The ONLY thing App keeps is `lastReport`: the execution result
 * of the order this particular browser submitted, which is private to this
 * client and never broadcast.
 */

import { useState } from "react";
import { ConnectionStatus } from "./components/ConnectionStatus";
import { ExecutionReport } from "./components/ExecutionReport";
import { OrderBook } from "./components/OrderBook";
import { OrderForm } from "./components/OrderForm";
import { Stats } from "./components/Stats";
import { TradeHistory } from "./components/TradeHistory";
import { useExchangeFeed } from "./websocket";
import type { ExecutionReport as Report } from "./types";

export default function App() {
  const { connection, book, trades, stats } = useExchangeFeed();
  const [lastReport, setLastReport] = useState<Report | null>(null);
  // Bumped on every submission so ExecutionReport remounts and clears the
  // cancel banner from the previous order.
  const [reportSeq, setReportSeq] = useState(0);

  function handleExecuted(report: Report) {
    setLastReport(report);
    setReportSeq((n) => n + 1);
  }

  return (
    <div className="min-h-screen bg-slate-950 text-slate-200">
      <header className="border-b border-slate-800 bg-slate-900/50">
        <div className="mx-auto flex max-w-[1400px] items-center justify-between px-6 py-4">
          <div>
            <h1 className="text-lg font-semibold tracking-tight text-slate-100">
              Low-Latency Trading Exchange
            </h1>
            <p className="text-[11px] text-slate-500">
              React · FastAPI · C++17 price-time priority matching engine
            </p>
          </div>
          <ConnectionStatus state={connection} />
        </div>
      </header>

      <main className="mx-auto grid max-w-[1400px] gap-4 px-6 py-6 lg:grid-cols-[320px_1fr_360px]">
        <div className="flex flex-col gap-4">
          <OrderForm onExecuted={handleExecuted} disabled={connection === "closed"} />
          <Stats stats={stats} />
        </div>

        <OrderBook book={book} />

        <div className="flex flex-col gap-4">
          <ExecutionReport key={reportSeq} report={lastReport} />
          <TradeHistory trades={trades} />
        </div>
      </main>

      <footer className="mx-auto max-w-[1400px] px-6 pb-6 text-[11px] leading-relaxed text-slate-600">
        The C++ engine is the source of truth for matching, order ids, the book
        and trades. FastAPI orchestrates; this UI only renders what the engine
        produced. Engine benchmark figures (≈15.3M orders/sec) measure the C++
        core in isolation, not this HTTP/WebSocket application.
      </footer>
    </div>
  );
}
