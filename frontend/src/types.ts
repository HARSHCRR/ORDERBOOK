/**
 * Shapes that come off the wire. These mirror the FastAPI response models in
 * server/models.py, which in turn mirror what engine_server.cpp emits.
 *
 * `null` on best_bid / best_ask / spread means "the engine has no such price"
 * (an empty side of the book). The C++ engine uses -1 as that sentinel; the
 * bridge converts it to null so the UI never sees a magic number.
 */

export type Side = "BUY" | "SELL";
export type OrderType = "LIMIT" | "MARKET";
export type OrderStatus = "FILLED" | "PARTIALLY_FILLED" | "RESTING" | "REJECTED";

export interface BookLevel {
  price: number;
  quantity: number;
}

export interface Book {
  bids: BookLevel[];
  asks: BookLevel[];
  best_bid: number | null;
  best_ask: number | null;
  spread: number | null;
}

export interface Trade {
  seq: number;
  /** -1 when the aggressor was a MARKET buy (no resting order id exists). */
  buy_id: number;
  /** -1 when the aggressor was a MARKET sell. */
  sell_id: number;
  price: number;
  quantity: number;
}

export interface Stats {
  orders_processed: number;
  trades_executed: number;
  active_orders: number;
  best_bid: number | null;
  best_ask: number | null;
  spread: number | null;
}

export interface ExecutionReport {
  order_id: number;
  side: Side;
  type: OrderType;
  price: number | null;
  requested_quantity: number;
  filled_quantity: number;
  remaining_quantity: number;
  resting: boolean;
  status: OrderStatus;
  trades: Trade[];
}

/** Events pushed by the server over /ws. */
export type ServerEvent =
  | { event: "SNAPSHOT"; book: Book; trades: Trade[]; stats: Stats }
  | { event: "BOOK_UPDATE"; book: Book }
  | { event: "TRADE"; trade: Trade }
  | { event: "STATS"; stats: Stats }
  | { event: "PONG" };

export type ConnectionState = "connecting" | "open" | "closed";
