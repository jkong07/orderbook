#pragma once

#include "orderbook/order.hpp"

#include <type_traits>

namespace orderbook {

// Doubly linked list of Order nodes, linked via Order::prev/Order::next
// (SPEC.md §4b Phase 6 step 2) instead of std::list<Order>'s separate
// per-element node allocation. Does NOT own the Order objects it links —
// OrderBook allocates them (currently plain new/delete — see SPEC.md §3
// Phase 6 step 4 for the planned memory-pool follow-up) and is responsible
// for freeing anything this class unlinks.
class IntrusiveOrderList {
public:
    template <bool IsConst>
    class Iterator {
    public:
        using NodePtr = std::conditional_t<IsConst, const Order*, Order*>;
        using Ref = std::conditional_t<IsConst, const Order&, Order&>;

        Iterator() = default;
        explicit Iterator(NodePtr node) : node_(node) {}

        Ref operator*() const { return *node_; }
        NodePtr operator->() const { return node_; }

        Iterator& operator++() {
            node_ = node_->next;
            return *this;
        }

        bool operator==(const Iterator& other) const { return node_ == other.node_; }
        bool operator!=(const Iterator& other) const { return !(*this == other); }

    private:
        NodePtr node_ = nullptr;
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    bool empty() const { return head_ == nullptr; }

    Order& front() const { return *head_; }

    void push_back(Order* node) {
        node->prev = tail_;
        node->next = nullptr;
        if (tail_) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        tail_ = node;
    }

    // Unlinks the node `it` points at. Does not delete it — the caller
    // (OrderBook) owns that decision, per the class comment.
    void erase(iterator it) {
        Order* node = &(*it);
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }
        node->prev = nullptr;
        node->next = nullptr;
    }

    // Unlinks and returns the head node (nullptr if empty) for the caller
    // to delete.
    Order* pop_front() {
        Order* node = head_;
        if (node) {
            erase(iterator{node});
        }
        return node;
    }

    iterator begin() { return iterator{head_}; }
    iterator end() { return iterator{nullptr}; }
    const_iterator begin() const { return const_iterator{head_}; }
    const_iterator end() const { return const_iterator{nullptr}; }

private:
    Order* head_ = nullptr;
    Order* tail_ = nullptr;
};

} // namespace orderbook
