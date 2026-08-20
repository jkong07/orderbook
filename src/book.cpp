#include "orderbook/book.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace orderbook {

OrderId OrderBook::next_id() {
    return next_seq_++;
}

OrderId OrderBook::add(Order order) {
    const OrderId id = order.order_id;
    const Side side = order.side;
    const Price price = order.price;

    // Pool-allocated so the node has a stable address for Order::prev/next
    // to point at (see SPEC.md §4b Phase 6 step 2), without a malloc/free
    // per order (step 4 — see order_pool.hpp).
    Order* node = pool_.acquire(std::move(order));

    if (side == Side::Buy) {
        bids_[price].push_back(node);
    } else {
        asks_[price].push_back(node);
    }
    index_[id] = IndexEntry{side, price, OrderList::iterator{node}};
    return id;
}

std::optional<Order> OrderBook::cancel(OrderId order_id) {
    auto index_it = index_.find(order_id);
    if (index_it == index_.end()) {
        return std::nullopt;
    }
    const auto [side, price, order_it] = index_it->second;
    Order* node = &(*order_it);

    auto erase_from = [&](auto& levels) -> Order {
        auto level_it = levels.find(price);
        OrderList& level = level_it->second;
        Order found = std::move(*node);
        found.prev = nullptr;
        found.next = nullptr;
        level.erase(order_it);
        pool_.release(node);
        if (level.empty()) {
            levels.erase(level_it);
        }
        return found;
    };

    Order found = (side == Side::Buy) ? erase_from(bids_) : erase_from(asks_);
    index_.erase(index_it);
    return found;
}

std::optional<Order> OrderBook::reduce(OrderId order_id, Qty qty) {
    auto index_it = index_.find(order_id);
    if (index_it == index_.end()) {
        return std::nullopt;
    }
    const auto [side, price, order_it] = index_it->second;
    Order* node = &(*order_it);

    node->qty -= qty;
    if (node->qty > 0) {
        Order snapshot = *node;
        snapshot.prev = nullptr;
        snapshot.next = nullptr;
        return snapshot;
    }

    auto erase_from = [&](auto& levels) -> Order {
        auto level_it = levels.find(price);
        OrderList& level = level_it->second;
        Order found = std::move(*node);
        found.qty = 0;
        found.prev = nullptr;
        found.next = nullptr;
        level.erase(order_it);
        pool_.release(node);
        if (level.empty()) {
            levels.erase(level_it);
        }
        return found;
    };

    Order found = (side == Side::Buy) ? erase_from(bids_) : erase_from(asks_);
    index_.erase(index_it);
    return found;
}

ExecuteResult OrderBook::execute(Order order) {
    ExecuteResult result;
    Qty remaining = order.qty;

    auto match_against = [&](auto& levels) {
        while (remaining > 0 && !levels.empty()) {
            auto level_it = levels.begin();
            const Price best_price = level_it->first;

            if (order.side == Side::Buy) {
                if (best_price > order.price) {
                    break;
                }
            } else {
                if (best_price < order.price) {
                    break;
                }
            }

            auto& level = level_it->second;
            Order& resting = level.front();
            const Qty trade_qty = std::min(remaining, resting.qty);

            remaining -= trade_qty;
            resting.qty -= trade_qty;

            result.fills.push_back(Fill{resting.order_id, best_price, trade_qty});

            if (resting.qty == 0) {
                index_.erase(resting.order_id);
                Order* node = level.pop_front();
                pool_.release(node);
                if (level.empty()) {
                    levels.erase(level_it);
                }
            }
        }
    };

    if (order.side == Side::Buy) {
        match_against(asks_);
    } else {
        match_against(bids_);
    }

    result.remaining_qty = remaining;
    return result;
}

std::pair<std::vector<std::pair<Price, Qty>>, std::vector<std::pair<Price, Qty>>>
OrderBook::depth(std::optional<std::size_t> n) const {
    auto aggregate = [&n](const auto& levels) {
        std::vector<std::pair<Price, Qty>> out;
        std::size_t count = 0;
        for (const auto& [price, level] : levels) {
            if (n && count >= *n) {
                break;
            }
            Qty total = 0;
            for (const auto& o : level) {
                total += o.qty;
            }
            out.emplace_back(price, total);
            ++count;
        }
        return out;
    };

    // bids_ and asks_ both iterate best-price-first from begin() (see
    // book.hpp), so no reversal is needed on either side.
    return {aggregate(bids_), aggregate(asks_)};
}

void OrderBook::printer(const std::vector<std::pair<Price, Qty>>& bid_list,
                         const std::vector<std::pair<Price, Qty>>& ask_list) const {
    std::cout << std::setw(8) << "PRICE" << ' ' << std::setw(8) << "QTY" << '\n';
    std::cout << std::string(20, '-') << '\n';

    if (bid_list.empty() && ask_list.empty()) {
        std::cout << "  (empty book)   \n";
        std::cout << std::string(20, '-') << '\n';
        return;
    }

    auto print_level = [](const std::pair<Price, Qty>& level) {
        std::cout << std::fixed << std::setprecision(2) << std::setw(8)
                   << (static_cast<double>(level.first) / 100.0) << ' ' << std::setw(8)
                   << level.second << '\n';
    };

    for (auto it = ask_list.rbegin(); it != ask_list.rend(); ++it) {
        print_level(*it);
    }

    std::cout << std::string(20, '-') << '\n';

    for (const auto& level : bid_list) {
        print_level(level);
    }
}

} // namespace orderbook
