#pragma once

#include "orderbook/intrusive_order_list.hpp"
#include "orderbook/types.hpp"

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace orderbook {

// Flat array of price levels indexed by (price - base_), replacing
// std::map<Price, OrderList>'s red-black tree to remove tree traversal and
// pointer-chasing from the hot path (SPEC.md §3 Phase 6 step 1). Grows on
// demand — at the back by appending, at the front by reallocating and
// shifting — to cover whatever price range actually appears; occupied
// slots are never physically pruned (an "empty" slot is just an empty
// list) since leaving them costs a few pointers and pruning would defeat
// the point of O(1) index access.
//
// `Descending` selects iteration order: false = ascending (asks — best is
// the lowest price, lowest index), true = descending (bids — best is the
// highest price, highest index). Iteration always walks best-to-worst via
// a tracked cursor so callers never reverse-iterate, mirroring the old
// std::map-based BidLevels/AskLevels split (see SPEC.md §4b).
template <bool Descending>
class PriceLevelArray {
public:
    using OrderList = IntrusiveOrderList;

    static constexpr std::size_t kEnd = std::numeric_limits<std::size_t>::max();

    template <bool IsConst>
    class Iterator {
    public:
        using ArrayPtr = std::conditional_t<IsConst, const PriceLevelArray*, PriceLevelArray*>;
        using ListRef = std::conditional_t<IsConst, const OrderList&, OrderList&>;
        using Value = std::pair<Price, ListRef>;

        Iterator() = default;
        Iterator(ArrayPtr array, std::size_t index) : array_(array), index_(index) {}

        Value operator*() const {
            return {array_->base_ + static_cast<Price>(index_), array_->levels_[index_]};
        }

        struct ArrowProxy {
            Value value;
            Value* operator->() { return &value; }
        };
        ArrowProxy operator->() const { return ArrowProxy{**this}; }

        Iterator& operator++() {
            index_ = array_->next_occupied(index_);
            return *this;
        }

        bool operator==(const Iterator& other) const {
            return array_ == other.array_ && index_ == other.index_;
        }
        bool operator!=(const Iterator& other) const { return !(*this == other); }

        std::size_t index() const { return index_; }

    private:
        ArrayPtr array_ = nullptr;
        std::size_t index_ = kEnd;
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    bool empty() const { return best_ == kEnd; }

    iterator begin() { return {this, best_}; }
    iterator end() { return {this, kEnd}; }
    const_iterator begin() const { return {this, best_}; }
    const_iterator end() const { return {this, kEnd}; }

    // Get-or-create the level at `price`, growing the array if needed.
    OrderList& operator[](Price price) {
        ensure_capacity(price);
        const std::size_t index = index_for(price);
        if (levels_[index].empty()) {
            note_occupied(index);
        }
        return levels_[index];
    }

    // `price` must already have a level allocated — every caller only
    // find()s a price it (or an earlier operator[]) already inserted via
    // OrderBook's index_ (see book.cpp), so no growth/bounds-check here.
    iterator find(Price price) { return {this, index_for(price)}; }

    // Called once the level `it` points at has just become empty (its
    // last order was erased). Advances the best cursor if `it` was best —
    // a no-op otherwise, since non-best empty slots don't affect iteration.
    void erase(iterator it) {
        if (it.index() == best_) {
            best_ = next_occupied(best_);
        }
    }

private:
    friend class Iterator<false>;
    friend class Iterator<true>;

    // Initial span and growth margin are in ticks (Price units, i.e.
    // cents — see SPEC.md §4b). 4096 ticks (~$40 at cent granularity)
    // comfortably covers a single stock's intraday range (the full AAPL
    // 2012-06-21 day spans ~1100 ticks) without the first insert forcing
    // an immediate regrow.
    static constexpr std::size_t kInitialSpan = 4096;
    static constexpr std::size_t kGrowMargin = 1024;

    std::size_t index_for(Price price) const {
        return static_cast<std::size_t>(price - base_);
    }

    void ensure_capacity(Price price) {
        if (levels_.empty()) {
            base_ = price - static_cast<Price>(kInitialSpan / 2);
            levels_.resize(kInitialSpan);
            return;
        }
        if (price < base_) {
            const std::size_t shift = static_cast<std::size_t>(base_ - price) + kGrowMargin;
            std::vector<OrderList> grown(levels_.size() + shift);
            for (std::size_t i = 0; i < levels_.size(); ++i) {
                grown[i + shift] = std::move(levels_[i]);
            }
            levels_ = std::move(grown);
            base_ -= static_cast<Price>(shift);
            if (best_ != kEnd) {
                best_ += shift;
            }
        }
        const std::size_t index = index_for(price);
        if (index >= levels_.size()) {
            levels_.resize(index + kGrowMargin + 1);
        }
    }

    // First occupied slot strictly beyond `from`, walking toward worse
    // prices (Descending: decreasing index; ascending: increasing index).
    // kEnd if none remain.
    std::size_t next_occupied(std::size_t from) const {
        if (from == kEnd) {
            return kEnd;
        }
        if constexpr (Descending) {
            for (std::size_t i = from; i-- > 0;) {
                if (!levels_[i].empty()) {
                    return i;
                }
            }
        } else {
            for (std::size_t i = from + 1; i < levels_.size(); ++i) {
                if (!levels_[i].empty()) {
                    return i;
                }
            }
        }
        return kEnd;
    }

    void note_occupied(std::size_t index) {
        if (best_ == kEnd) {
            best_ = index;
            return;
        }
        if constexpr (Descending) {
            if (index > best_) {
                best_ = index;
            }
        } else {
            if (index < best_) {
                best_ = index;
            }
        }
    }

    std::vector<OrderList> levels_;
    Price base_ = 0;
    std::size_t best_ = kEnd;
};

using BidLevelArray = PriceLevelArray<true>;
using AskLevelArray = PriceLevelArray<false>;

} // namespace orderbook
