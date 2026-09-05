"""
Pydantic models for the HTTP surface.

These exist for two reasons only:

  1. validate/coerce what comes in on POST /orders before it reaches the C++
     process, so the engine never has to defend against garbage; and
  2. give FastAPI enough type information to generate /docs.

They deliberately do NOT re-model the engine's internal state.  The C++ engine
is the single source of truth for the book, the trades and the order ids; the
response models below just describe the shape it already sends back.
"""

from enum import Enum
from typing import List, Optional

from pydantic import BaseModel, Field, model_validator


class Side(str, Enum):
    BUY = "BUY"
    SELL = "SELL"


class OrderType(str, Enum):
    LIMIT = "LIMIT"
    MARKET = "MARKET"


class OrderStatus(str, Enum):
    FILLED = "FILLED"
    PARTIALLY_FILLED = "PARTIALLY_FILLED"
    RESTING = "RESTING"
    REJECTED = "REJECTED"


# ---------------------------------------------------------------------------
# Requests
# ---------------------------------------------------------------------------
class PlaceOrderRequest(BaseModel):
    side: Side
    type: OrderType = OrderType.LIMIT
    # The engine works in integer prices and quantities (see OrderBook.hpp).
    # Keeping the API integral too avoids any float rounding between the two
    # layers -- there is exactly one representation of a price in this system.
    price: Optional[int] = Field(default=None, gt=0)
    quantity: int = Field(gt=0)

    @model_validator(mode="after")
    def _price_rules(self) -> "PlaceOrderRequest":
        if self.type == OrderType.LIMIT and self.price is None:
            raise ValueError("LIMIT orders require a price")
        if self.type == OrderType.MARKET and self.price is not None:
            raise ValueError("MARKET orders must not carry a price")
        return self


# ---------------------------------------------------------------------------
# Responses  (shapes mirror what engine_server.cpp emits)
# ---------------------------------------------------------------------------
class TradeModel(BaseModel):
    seq: int
    buy_id: int      # -1 when the aggressor was a MARKET buy
    sell_id: int     # -1 when the aggressor was a MARKET sell
    price: int
    quantity: int


class PlaceOrderResponse(BaseModel):
    order_id: int
    side: Side
    type: OrderType
    price: Optional[int] = None
    requested_quantity: int
    filled_quantity: int
    remaining_quantity: int
    resting: bool
    status: OrderStatus
    trades: List[TradeModel] = []


class BookLevel(BaseModel):
    price: int
    quantity: int


class BookResponse(BaseModel):
    bids: List[BookLevel]
    asks: List[BookLevel]
    best_bid: Optional[int] = None
    best_ask: Optional[int] = None
    spread: Optional[int] = None


class TradesResponse(BaseModel):
    trades: List[TradeModel]
    total: int


class StatsResponse(BaseModel):
    orders_processed: int
    trades_executed: int
    active_orders: int
    best_bid: Optional[int] = None
    best_ask: Optional[int] = None
    spread: Optional[int] = None


class CancelResponse(BaseModel):
    order_id: int
    cancelled: bool
    status: str


class OrderResponse(BaseModel):
    order_id: int
    resting: bool
    status: str
    side: Optional[Side] = None
    price: Optional[int] = None
    remaining_quantity: Optional[int] = None


class HealthResponse(BaseModel):
    status: str
    engine_alive: bool
    engine_binary: str
