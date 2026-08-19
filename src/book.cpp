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
    if (order.side == Side::Buy) {
        bids_[order.price].push_back(std::move(order));
    } else {
        asks_[order.price].push_back(std::move(order));
    }
    return id;
}

std::optional<Order> OrderBook::cancel(OrderId order_id) {
    auto search_side = [&order_id](auto& levels) -> std::optional<Order> {
        for (auto level_it = levels.begin(); level_it != levels.end(); ++level_it) {
            auto& [price, level] = *level_it;
            for (auto order_it = level.begin(); order_it != level.end(); ++order_it) {
                if (order_it->order_id == order_id) {
                    Order found = std::move(*order_it);
                    level.erase(order_it);
                    if (level.empty()) {
                        levels.erase(level_it);
                    }
                    return found;
                }
            }
        }
        return std::nullopt;
    };

    if (auto found = search_side(bids_)) {
        return found;
    }
    return search_side(asks_);
}

std::optional<Order> OrderBook::reduce(OrderId order_id, Qty qty) {
    auto search_side = [&order_id, &qty](auto& levels) -> std::optional<Order> {
        for (auto level_it = levels.begin(); level_it != levels.end(); ++level_it) {
            auto& [price, level] = *level_it;
            for (auto order_it = level.begin(); order_it != level.end(); ++order_it) {
                if (order_it->order_id != order_id) {
                    continue;
                }
                order_it->qty -= qty;
                if (order_it->qty <= 0) {
                    Order found = std::move(*order_it);
                    found.qty = 0;
                    level.erase(order_it);
                    if (level.empty()) {
                        levels.erase(level_it);
                    }
                    return found;
                }
                return *order_it;
            }
        }
        return std::nullopt;
    };

    if (auto found = search_side(bids_)) {
        return found;
    }
    return search_side(asks_);
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
                level.pop_front();
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
