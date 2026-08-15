import random

from book import OrderBook, Order, Side
from invariants import check_all_state_invariants, check_conservation


def generate_events(rng: random.Random, n: int, start_price: int = 10_000, tick: int = 1) -> list[dict]:
    """Pre-generate a fixed list of events.

    add/cancel prices use tight jitter around the reference so orders stack
    up on the same handful of levels (a thick book). execute prices are
    deliberately pushed several ticks through the market on their own side,
    so they reliably sweep multiple stacked levels instead of tapping the
    top level and stopping. Quantities stay single-digit so partial fills
    and limit-stopped walks get exercised.
    """
    events = []
    ref = start_price

    for _ in range(n):
        kind = rng.choices(["add", "cancel", "execute"], weights=[60, 30, 10])[0]

        ref += rng.choice([-1, 0, 1]) * tick
        side = rng.choice([Side.BUY, Side.SELL])

        if kind == "execute":
            reach = rng.randint(3, 10) * tick
            price = ref + reach if side == Side.BUY else ref - reach
        else:
            jitter = rng.randint(-2, 2) * tick
            price = ref + jitter
        price = max(tick, price)

        events.append({
            "kind": kind,
            "side": side,
            "price": price,
            "qty": rng.randint(1, 9),
        })

    return events


def run_driver(n_events: int = 500, seed: int | None = None) -> list[dict]:
    if seed is None:
        seed = random.SystemRandom().randrange(2**32)
    print(f"seed={seed}")

    rng = random.Random(seed)
    book = OrderBook()
    events = generate_events(rng, n_events)

    # Order is a mutable dataclass, and book.add() stores this exact object
    # reference in its deque — so once an order rests, book.execute() mutates
    # its .qty in place on the *same* object we're holding here. That lets us
    # tell whether a fill fully drained a resting order (qty == 0, no longer
    # in the book) without ever rescanning the book itself.
    order_by_id: dict[int, Order] = {}
    live_ids: set[int] = set()
    log: list[dict] = []

    added_buy = added_sell = 0
    cancelled_buy = cancelled_sell = 0
    filled_buy = filled_sell = 0

    def apply_fills(fills):
        for resting_id, _, _ in fills:
            if order_by_id[resting_id].qty == 0:
                live_ids.discard(resting_id)
                del order_by_id[resting_id]

    for seq, ev in enumerate(events):
        kind = ev["kind"]
        if kind == "cancel" and not live_ids:
            kind = "add"

        if kind == "add":
            # Limit order: match whatever crosses, then rest the remainder.
            # Never left as a raw insert, since a randomly clustered price
            # can easily land on the wrong side of the market.
            order_id = book._next_id()
            order = Order(order_id, ev["side"], ev["price"], ev["qty"])
            fills = book.execute(order)
            apply_fills(fills)
            fill_qty = sum(q for _, _, q in fills)
            # fills deplete the resting side, which is opposite the aggressor
            if order.side == Side.BUY:
                filled_sell += fill_qty
            else:
                filled_buy += fill_qty

            if order.qty > 0:
                book.add(order)
                order_by_id[order_id] = order
                live_ids.add(order_id)
                if order.side == Side.BUY:
                    added_buy += order.qty
                else:
                    added_sell += order.qty

            record = {
                "seq": seq, "kind": "add", "order_id": order_id,
                "side": ev["side"].value, "price": ev["price"], "qty": ev["qty"],
                "fills": fills, "resting_qty": order.qty,
            }

        elif kind == "cancel":
            cancel_id = rng.choice(sorted(live_ids))
            removed = book.cancel(cancel_id)
            live_ids.discard(cancel_id)
            del order_by_id[cancel_id]
            if removed.side == Side.BUY:
                cancelled_buy += removed.qty
            else:
                cancelled_sell += removed.qty
            record = {
                "seq": seq, "kind": "cancel", "order_id": cancel_id,
                "remaining_qty": removed.qty,
            }

        else:  # execute: marketable sweep (IOC) — unfilled leftover is dropped, not rested
            order_id = book._next_id()
            order = Order(order_id, ev["side"], ev["price"], ev["qty"])
            fills = book.execute(order)
            apply_fills(fills)
            fill_qty = sum(q for _, _, q in fills)
            if order.side == Side.BUY:
                filled_sell += fill_qty
            else:
                filled_buy += fill_qty

            record = {
                "seq": seq, "kind": "execute", "order_id": order_id,
                "side": ev["side"].value, "price": ev["price"], "qty": ev["qty"],
                "fills": fills, "dropped_qty": order.qty,
            }

        log.append(record)

        check_all_state_invariants(book)
        check_conservation(
            book,
            added_buy=added_buy, cancelled_buy=cancelled_buy, filled_buy=filled_buy,
            added_sell=added_sell, cancelled_sell=cancelled_sell, filled_sell=filled_sell,
        )

    return log


if __name__ == "__main__":
    run_driver()
