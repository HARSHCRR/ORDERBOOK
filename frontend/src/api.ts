/**
 * Thin HTTP client. Every call goes through the Vite dev proxy at /api, which
 * forwards to FastAPI (see vite.config.ts), so the browser never needs CORS.
 *
 * Nothing here caches. The C++ engine is the source of truth and the UI keeps
 * its live state from the WebSocket feed instead; these calls are used for the
 * initial load and for actions (place / cancel).
 */

import type { Book, ExecutionReport, OrderType, Side, Stats, Trade } from "./types";

const BASE = "/api";

/** Surfaces FastAPI's `detail` field so the UI can show a real reason. */
export class ApiError extends Error {
  status: number;

  constructor(status: number, message: string) {
    super(message);
    this.status = status;
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(BASE + path, {
    headers: { "Content-Type": "application/json" },
    ...init,
  });

  if (!response.ok) {
    let detail = `${response.status} ${response.statusText}`;
    try {
      const body = await response.json();
      if (typeof body.detail === "string") {
        detail = body.detail;
      } else if (Array.isArray(body.detail)) {
        // pydantic validation errors arrive as a list
        detail = body.detail.map((d: { msg: string }) => d.msg).join("; ");
      }
    } catch {
      /* response had no JSON body; keep the status line */
    }
    throw new ApiError(response.status, detail);
  }

  return response.json() as Promise<T>;
}

export const api = {
  health: () => request<{ status: string; engine_alive: boolean }>("/health"),

  book: (depth = 15) => request<Book>(`/book?depth=${depth}`),

  trades: (limit = 50) => request<{ trades: Trade[]; total: number }>(`/trades?limit=${limit}`),

  stats: () => request<Stats>("/stats"),

  placeOrder: (order: { side: Side; type: OrderType; price?: number; quantity: number }) =>
    request<ExecutionReport>("/orders", {
      method: "POST",
      body: JSON.stringify(order),
    }),

  cancelOrder: (orderId: number) =>
    request<{ order_id: number; cancelled: boolean; status: string }>(`/orders/${orderId}`, {
      method: "DELETE",
    }),
};
