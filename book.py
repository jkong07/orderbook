from enum import Enum
from sortedcontainers import SortedDict
from collections import deque
from dataclasses import dataclass

class Side(Enum):
    BUY = "buy"
    SELL = "sell"

@dataclass
class Order:
    order_id: int
    side: Side
    price: int
    qty: int

class OrderBook:
    def __init__(self):
        self.bids = SortedDict() # price -> deque[Order]
        self.asks = SortedDict() # price -> deque[Order]
        self._next_seq = 0

    def _next_id(self) -> int:
        i = self._next_seq
        self._next_seq += 1
        return i

    def add(self, side: Side, price: int, qty: int) -> int:
        order_id = self._next_id()
        order = Order(order_id, side, price, qty)

        if side == Side.BUY:
            if price not in self.bids:
                self.bids[price] = deque()
            self.bids[price].append(order)
        else:
            if price not in self.asks:
                self.asks[price] = deque()
            self.asks[price].append(order)

        return order_id

    def cancel(self, order_id: int) -> bool:
        for book_side in (self.bids, self.asks):
            for price, price_deque in book_side.items():
                for o in price_deque:
                    if o.order_id == order_id:
                        price_deque.remove(o)
                        if not price_deque:
                            del book_side[price]
                        return True

        return False
        



    









    

