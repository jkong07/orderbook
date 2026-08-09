from enum import Enum
from sortedcontainers import SortedDict
from collections import deque

class Side(Enum):
    BUY = "buy"
    SELL = "sell"

class Order:
    def __init__(self, order_id: int, side: Side, price: int, qty: int):
        self.order_id = order_id
        self.side = side
        self.price = price
        self.qty = qty

class OrderBook:
    def __init__(self):
        self.bids = SortedDict()
        self.asks = SortedDict()
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


    









    

