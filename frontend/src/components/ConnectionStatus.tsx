import type { ConnectionState } from "../types";

const LABEL: Record<ConnectionState, string> = {
  connecting: "Connecting",
  open: "Live",
  closed: "Disconnected",
};

const DOT: Record<ConnectionState, string> = {
  connecting: "bg-amber-400 animate-pulse",
  open: "bg-emerald-400",
  closed: "bg-rose-500",
};

const TEXT: Record<ConnectionState, string> = {
  connecting: "text-amber-300",
  open: "text-emerald-300",
  closed: "text-rose-300",
};

export function ConnectionStatus({ state }: { state: ConnectionState }) {
  return (
    <div className="flex items-center gap-2 rounded-full border border-slate-800 bg-slate-900/70 px-3 py-1.5">
      <span className={`h-2 w-2 rounded-full ${DOT[state]}`} />
      <span className={`text-xs font-medium ${TEXT[state]}`}>{LABEL[state]}</span>
      <span className="text-[10px] uppercase tracking-wider text-slate-600">WS</span>
    </div>
  );
}
