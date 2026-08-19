#include "orderbook/invariants.hpp"

#include <set>
#include <sstream>

namespace orderbook {

std::optional<std::string> check_not_crossed(const OrderBook& book) {
    if (book.bids().empty() || book.asks().empty()) {
        return std::nullopt;
    }
    const Price best_bid = book.bids().begin()->first;
    const Price best_ask = book.asks().begin()->first;
    if (best_bid >= best_ask) {
        std::ostringstream out;
        out << "book crossed or locked: best_bid=" << best_bid << " best_ask=" << best_ask;
        return out.str();
    }
    return std::nullopt;
}

std::optional<std::string> check_positive_qty(const OrderBook& book) {
    for (const auto& [price, level] : book.bids()) {
        for (const auto& o : level) {
            if (o.qty <= 0) {
                std::ostringstream out;
                out << "order " << o.order_id << " at price " << price << " has non-positive qty "
                    << o.qty;
                return out.str();
            }
        }
    }
    for (const auto& [price, level] : book.asks()) {
        for (const auto& o : level) {
            if (o.qty <= 0) {
                std::ostringstream out;
                out << "order " << o.order_id << " at price " << price << " has non-positive qty "
                    << o.qty;
                return out.str();
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> check_unique_ids(const OrderBook& book) {
    std::set<OrderId> seen;
    for (const auto& [price, level] : book.bids()) {
        for (const auto& o : level) {
            if (!seen.insert(o.order_id).second) {
                std::ostringstream out;
                out << "order id " << o.order_id << " appears more than once";
                return out.str();
            }
        }
    }
    for (const auto& [price, level] : book.asks()) {
        for (const auto& o : level) {
            if (!seen.insert(o.order_id).second) {
                std::ostringstream out;
                out << "order id " << o.order_id << " appears more than once";
                return out.str();
            }
        }
    }
    return std::nullopt;
}

namespace {

Qty resting_qty(const auto& levels) {
    Qty total = 0;
    for (const auto& [price, level] : levels) {
        for (const auto& o : level) {
            total += o.qty;
        }
    }
    return total;
}

} // namespace

std::optional<std::string> check_conservation(const OrderBook& book, const Tally& tally) {
    const Qty resting_buy = resting_qty(book.bids());
    const Qty resting_sell = resting_qty(book.asks());

    const Qty expected_buy = tally.added_buy - tally.cancelled_buy - tally.filled_buy;
    if (expected_buy != resting_buy) {
        std::ostringstream out;
        out << "conservation violated (bids): added_buy(" << tally.added_buy << ") - cancelled_buy("
            << tally.cancelled_buy << ") - filled_buy(" << tally.filled_buy << ") = " << expected_buy
            << ", but resting bid qty = " << resting_buy;
        return out.str();
    }

    const Qty expected_sell = tally.added_sell - tally.cancelled_sell - tally.filled_sell;
    if (expected_sell != resting_sell) {
        std::ostringstream out;
        out << "conservation violated (asks): added_sell(" << tally.added_sell << ") - cancelled_sell("
            << tally.cancelled_sell << ") - filled_sell(" << tally.filled_sell << ") = " << expected_sell
            << ", but resting ask qty = " << resting_sell;
        return out.str();
    }

    return std::nullopt;
}

std::optional<std::string> check_all(const OrderBook& book, const Tally& tally) {
    if (auto violation = check_not_crossed(book)) {
        return violation;
    }
    if (auto violation = check_positive_qty(book)) {
        return violation;
    }
    if (auto violation = check_unique_ids(book)) {
        return violation;
    }
    return check_conservation(book, tally);
}

} // namespace orderbook
