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

    def add(self, order : Order) -> int:
        order_id = self._next_id()

        order_dict = self.bids if order.side == Side.BUY else self.asks
        if order.price not in order_dict:
            order_dict[order.price] = deque()
        order_dict[order.price].append(order)

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

    def execute(self, order: Order) -> None:
        exec_dict = self.asks if order.side == Side.BUY else self.bids
        i = 0 if order.side == Side.BUY else -1

        while order.qty > 0 and exec_dict:
            best_price, level = exec_dict.peekitem(i)

            if order.side == Side.BUY:
                if best_price > order.price:
                    break
            else:
                if best_price < order.price:
                    break

            resting = level[0]
            trade_qty = min(order.qty, resting.qty)

            order.qty -= trade_qty
            resting.qty -= trade_qty

            if resting.qty == 0:
                level.popleft()
                if not level:
                    del exec_dict[best_price]

    def depth(self, n=None):
        if n is not None and n < 0:
            raise ValueError(f"n must be non-negative, got {n}")
        bid_levels = []
        ask_levels = []
        if n is None:
            for price in self.bids.islice(reverse=True):
                count = 0
                for o in self.bids[price]:
                    count += o.qty
                bid_levels.append((price, count))

            for price in self.asks.islice():
                count = 0
                for o in self.asks[price]:
                    count += o.qty
                ask_levels.append((price, count))
        else:
            bid_depth = min(n, len(self.bids))
            ask_depth = min(n, len(self.asks))

            for price in self.bids.islice(start=len(self.bids) - bid_depth, reverse = True):
                count = 0
                for o in self.bids[price]:
                    count += o.qty
                bid_levels.append((price, count))
            
            for price in self.asks.islice(stop=ask_depth):
                count = 0
                for o in self.asks[price]:
                    count += o.qty
                ask_levels.append((price, count))

        return bid_levels, ask_levels

    def printer(self, bid_list: list[tuple[int, int]], ask_list: list[tuple[int, int]]):
        print(f"{'PRICE':>8} {'QTY':>8}")
        print("-" * 20)

        if not bid_list and not ask_list:
            print(f"{'(empty book)':^17}")
            print("-" * 20)
            return

        for p in reversed(ask_list):
            row = f"{p[0]/100:>8.2f} {p[1]:>8}"
            print(row)

        print("-" * 20)

        for p in bid_list:
            row = f"{p[0]/100:>8.2f} {p[1]:>8}"
            print(row)
                




    









    

