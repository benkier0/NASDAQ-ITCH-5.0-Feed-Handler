#pragma once
#include "order.hpp"
#include "../itch/messages.hpp"
#include <cstdint>
#include <map>
#include <unordered_map>
#include <functional>
#include <array>

// L2 order book: aggregates resting shares by price level per stock.
// All on_* methods decode big-endian fields on entry.
// Indexed by stock_locate (0–8191) for O(1) book access.

namespace book {

static constexpr uint16_t MAX_STOCKS = 8192;

struct PriceLevel {
    uint64_t total_shares{0};
    uint32_t order_count{0};
};

// Per-stock book: descending bids, ascending asks.
struct L2Book {
    std::map<uint32_t, PriceLevel, std::greater<uint32_t>> bids;
    std::map<uint32_t, PriceLevel>                         asks;
    std::unordered_map<uint64_t, Order>                    orders;  // ref -> Order

    [[nodiscard]] bool empty() const noexcept {
        return bids.empty() && asks.empty();
    }

    // Best bid/ask prices (0 if side is empty)
    [[nodiscard]] uint32_t best_bid() const noexcept {
        return bids.empty() ? 0u : bids.begin()->first;
    }
    [[nodiscard]] uint32_t best_ask() const noexcept {
        return asks.empty() ? 0u : asks.begin()->first;
    }
};

class OrderBook {
public:
    OrderBook();

    // ── ITCH 5.0 message handlers (fields still big-endian) ─────────────────
    void on_add_order(const itch::AddOrder& msg) noexcept;
    void on_add_order_mpid(const itch::AddOrderMPID& msg) noexcept;
    void on_order_executed(const itch::OrderExecuted& msg) noexcept;
    void on_order_executed_with_price(const itch::OrderExecutedWithPrice& msg) noexcept;
    void on_order_cancel(const itch::OrderCancel& msg) noexcept;
    void on_order_delete(const itch::OrderDelete& msg) noexcept;
    void on_order_replace(const itch::OrderReplace& msg) noexcept;
    void on_system_event(const itch::SystemEvent& msg) noexcept;

    [[nodiscard]] const L2Book& book(uint16_t locate) const noexcept {
        return books_[locate];
    }

    [[nodiscard]] uint64_t msgs_processed() const noexcept { return msgs_processed_; }

private:
    void add_order(uint16_t locate, uint64_t ref, char side,
                   uint32_t shares, uint32_t price) noexcept;
    void reduce_order(uint64_t ref, uint32_t shares) noexcept;
    void delete_order(uint64_t ref) noexcept;

    std::array<L2Book, MAX_STOCKS> books_;
    std::unordered_map<uint64_t, uint16_t> ref_to_locate_;  // global ref → locate
    uint64_t msgs_processed_{0};
};

} // namespace book
