from book import OrderBook, Side


class InvariantViolation(Exception):
    pass


def check_not_crossed(book: OrderBook) -> None:
    if not book.bids or not book.asks:
        return
    best_bid = book.bids.peekitem(-1)[0]
    best_ask = book.asks.peekitem(0)[0]
    if best_bid >= best_ask:
        raise InvariantViolation(
            f"book crossed or locked: best_bid={best_bid} best_ask={best_ask}"
        )


def check_positive_qty(book: OrderBook) -> None:
    for book_side in (book.bids, book.asks):
        for price, level in book_side.items():
            for o in level:
                if o.qty <= 0:
                    raise InvariantViolation(
                        f"order {o.order_id} at price {price} has non-positive qty {o.qty}"
                    )


def check_no_empty_levels(book: OrderBook) -> None:
    for book_side in (book.bids, book.asks):
        for price, level in book_side.items():
            if not level:
                raise InvariantViolation(f"empty level present at price {price}")


def check_price_side_consistency(book: OrderBook) -> None:
    for book_side, side, name in (
        (book.bids, Side.BUY, "bids"),
        (book.asks, Side.SELL, "asks"),
    ):
        for price, level in book_side.items():
            for o in level:
                if o.price != price:
                    raise InvariantViolation(
                        f"order {o.order_id} price {o.price} does not match level key {price}"
                    )
                if o.side != side:
                    raise InvariantViolation(
                        f"order {o.order_id} side {o.side} found in {name} book"
                    )


def check_fifo_order(book: OrderBook) -> None:
    for book_side in (book.bids, book.asks):
        for price, level in book_side.items():
            prev_id = None
            for o in level:
                if prev_id is not None and o.order_id <= prev_id:
                    raise InvariantViolation(
                        f"FIFO violated at price {price}: order {o.order_id} follows {prev_id}"
                    )
                prev_id = o.order_id


def check_unique_ids(book: OrderBook) -> None:
    seen = set()
    for book_side in (book.bids, book.asks):
        for price, level in book_side.items():
            for o in level:
                if o.order_id in seen:
                    raise InvariantViolation(f"order id {o.order_id} appears more than once")
                seen.add(o.order_id)


def check_conservation(
    book: OrderBook,
    added_buy: int, cancelled_buy: int, filled_buy: int,
    added_sell: int, cancelled_sell: int, filled_sell: int,
) -> None:
    # Per-side, not global: summing both sides first would let qty silently
    # migrate bid->ask (or vice versa) without tripping this check.
    #
    # `filled_buy`/`filled_sell` are counted once, not twice: a trade removes
    # qty from a resting order on one side AND from the aggressive order on
    # the other, but "added" here only counts qty that was actually persisted
    # via book.add() — the driver never adds the matched-away portion of an
    # aggressive order, so that qty was never "added" in the first place and
    # doesn't need a second subtraction. If your driver instead counts an
    # aggressive order's full original qty as "added", you must double the
    # filled term on the side it depleted, or this check is off by exactly 2x.
    resting_buy = sum(o.qty for level in book.bids.values() for o in level)
    resting_sell = sum(o.qty for level in book.asks.values() for o in level)

    expected_buy = added_buy - cancelled_buy - filled_buy
    if expected_buy != resting_buy:
        raise InvariantViolation(
            f"conservation violated (bids): added_buy({added_buy}) - cancelled_buy({cancelled_buy}) "
            f"- filled_buy({filled_buy}) = {expected_buy}, but resting bid qty = {resting_buy}"
        )

    expected_sell = added_sell - cancelled_sell - filled_sell
    if expected_sell != resting_sell:
        raise InvariantViolation(
            f"conservation violated (asks): added_sell({added_sell}) - cancelled_sell({cancelled_sell}) "
            f"- filled_sell({filled_sell}) = {expected_sell}, but resting ask qty = {resting_sell}"
        )


def check_all_state_invariants(book: OrderBook) -> None:
    check_not_crossed(book)
    check_positive_qty(book)
    check_no_empty_levels(book)
    check_price_side_consistency(book)
    check_fifo_order(book)
    check_unique_ids(book)
