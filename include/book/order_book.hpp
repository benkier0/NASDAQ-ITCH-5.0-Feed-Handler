#pragma once
#include "order.hpp"
#include "../itch/messages.hpp"
#include <cstdint>
#include <unordered_map>
#include <array>
#include <limits>

namespace book {

static constexpr uint16_t MAX_STOCKS = 8192;

struct PriceLevel {
    uint64_t total_shares{0};
    uint32_t order_count{0};
};

// Per-stock book: flat hash maps for O(1) level lookup on the hot path.
// best_bid/best_ask do a linear scan — they are not called on the receive path.
struct L2Book {
    std::unordered_map<uint32_t, PriceLevel> bids;
    std::unordered_map<uint32_t, PriceLevel> asks;

    [[nodiscard]] bool empty() const noexcept {
        return bids.empty() && asks.empty();
    }

    [[nodiscard]] uint32_t best_bid() const noexcept {
        uint32_t best = 0;
        for (const auto& [px, lvl] : bids) if (px > best) best = px;
        return best;
    }
    [[nodiscard]] uint32_t best_ask() const noexcept {
        uint32_t best = std::numeric_limits<uint32_t>::max();
        for (const auto& [px, lvl] : asks) if (px < best) best = px;
        return best == std::numeric_limits<uint32_t>::max() ? 0u : best;
    }
};

class OrderBook {
public:
    OrderBook();

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
    std::unordered_map<uint64_t, Order> orders_;
    uint64_t msgs_processed_{0};
};

} // namespace book
