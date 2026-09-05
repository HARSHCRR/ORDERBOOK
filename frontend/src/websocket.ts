/**
 * useExchangeFeed -- the live connection to FastAPI's /ws.
 *
 * This hook holds ALL real-time state for the dashboard: the book, the trade
 * tape, the stats and the connection status. There is no polling anywhere in
 * the app; the server pushes and this hook applies.
 *
 * Message handling is deliberately dumb:
 *   SNAPSHOT    -> replace everything (sent once, on connect)
 *   BOOK_UPDATE -> replace the book
 *   TRADE       -> prepend to the tape
 *   STATS       -> replace the counters
 *
 * The server is the only writer, so the client never merges or reconciles --
 * whatever arrives last is the truth. Reconnection is a fixed 2s retry, which
 * is all a localhost dev setup needs.
 */

import { useCallback, useEffect, useRef, useState } from "react";
import type { Book, ConnectionState, ServerEvent, Stats, Trade } from "./types";

const RECONNECT_DELAY_MS = 2000;
const MAX_TAPE = 50;

const EMPTY_BOOK: Book = {
  bids: [],
  asks: [],
  best_bid: null,
  best_ask: null,
  spread: null,
};

const EMPTY_STATS: Stats = {
  orders_processed: 0,
  trades_executed: 0,
  active_orders: 0,
  best_bid: null,
  best_ask: null,
  spread: null,
};

export function useExchangeFeed() {
  const [connection, setConnection] = useState<ConnectionState>("connecting");
  const [book, setBook] = useState<Book>(EMPTY_BOOK);
  const [trades, setTrades] = useState<Trade[]>([]);
  const [stats, setStats] = useState<Stats>(EMPTY_STATS);

  const socketRef = useRef<WebSocket | null>(null);
  const retryRef = useRef<number | null>(null);
  // Set during cleanup so a scheduled retry does not fire after unmount.
  const closedRef = useRef(false);

  const connect = useCallback(() => {
    if (closedRef.current) return;

    setConnection("connecting");

    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const socket = new WebSocket(`${protocol}//${window.location.host}/ws`);
    socketRef.current = socket;

    socket.onopen = () => {
      if (socketRef.current !== socket) return;
      setConnection("open");
    };

    socket.onmessage = (raw) => {
      if (socketRef.current !== socket) return;

      let message: ServerEvent;
      try {
        message = JSON.parse(raw.data);
      } catch {
        return; // ignore anything that is not our protocol
      }

      switch (message.event) {
        case "SNAPSHOT":
          setBook(message.book);
          // The server sends the tape oldest-first; the UI shows newest-first.
          setTrades([...message.trades].reverse().slice(0, MAX_TAPE));
          setStats(message.stats);
          break;
        case "BOOK_UPDATE":
          setBook(message.book);
          break;
        case "TRADE":
          setTrades((previous) => [message.trade, ...previous].slice(0, MAX_TAPE));
          break;
        case "STATS":
          setStats(message.stats);
          break;
        default:
          break;
      }
    };

    socket.onclose = () => {
      // Only the CURRENT socket may drive state or schedule a reconnect.
      //
      // React StrictMode mounts effects twice in development: the first socket
      // is closed by cleanup, then a second is opened. The first socket's
      // onclose lands asynchronously, after the remount has already cleared
      // closedRef -- so without this identity check it would reconnect itself
      // and leave TWO live sockets pushing into the same state, duplicating
      // every trade. Comparing against socketRef.current makes a superseded
      // socket inert.
      if (socketRef.current !== socket) return;

      setConnection("closed");
      if (!closedRef.current) {
        retryRef.current = window.setTimeout(connect, RECONNECT_DELAY_MS);
      }
    };

    // onerror always precedes onclose; let onclose own the retry.
    socket.onerror = () => socket.close();
  }, []);

  useEffect(() => {
    closedRef.current = false;
    connect();

    return () => {
      closedRef.current = true;
      if (retryRef.current) window.clearTimeout(retryRef.current);
      socketRef.current?.close();
    };
  }, [connect]);

  return { connection, book, trades, stats };
}
