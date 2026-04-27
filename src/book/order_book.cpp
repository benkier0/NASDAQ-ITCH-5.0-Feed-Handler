#include "../../include/book/order_book.hpp"
#include "../../include/itch/messages.hpp"
#include <cstring>

namespace book {

OrderBook::OrderBook() {
    orders_.reserve(1 << 20);  // 1M live orders; single map replaces ref_to_locate_ + per-book orders
}

void OrderBook::add_order(uint16_t locate, uint64_t ref,
                          char side, uint32_t shares, uint32_t price) noexcept {
    orders_.emplace(ref, Order{ref, price, shares, locate, side});
    auto& bk = books_[locate];
    auto& level_map = (side == 'B') ? bk.bids : bk.asks;
    auto& level = level_map[price];
    level.total_shares += shares;
    ++level.order_count;
}

void OrderBook::reduce_order(uint64_t ref, uint32_t shares) noexcept {
    auto it = orders_.find(ref);
    if (__builtin_expect(it == orders_.end(), 0)) return;

    Order& ord = it->second;
    const uint32_t remove = (shares > ord.shares) ? ord.shares : shares;
    ord.shares -= remove;

    auto& bk = books_[ord.stock_locate];
    auto reduce_level = [&](auto& level_map) {
        auto lvl_it = level_map.find(ord.price);
        if (lvl_it == level_map.end()) return;
        lvl_it->second.total_shares -= remove;
        if (ord.shares == 0) {
            --lvl_it->second.order_count;
            if (lvl_it->second.order_count == 0) level_map.erase(lvl_it);
        }
    };

    if (ord.side == 'B') reduce_level(bk.bids);
    else                  reduce_level(bk.asks);

    if (ord.shares == 0) orders_.erase(it);
}

void OrderBook::delete_order(uint64_t ref) noexcept {
    auto it = orders_.find(ref);
    if (__builtin_expect(it == orders_.end(), 0)) return;

    const Order& ord = it->second;
    auto& bk = books_[ord.stock_locate];

    auto remove_from = [&](auto& level_map) {
        auto lvl_it = level_map.find(ord.price);
        if (lvl_it == level_map.end()) return;
        lvl_it->second.total_shares -= ord.shares;
        --lvl_it->second.order_count;
        if (lvl_it->second.order_count == 0) level_map.erase(lvl_it);
    };

    if (ord.side == 'B') remove_from(bk.bids);
    else                  remove_from(bk.asks);

    orders_.erase(it);
}

void OrderBook::on_add_order(const itch::AddOrder& msg) noexcept {
    ++msgs_processed_;
    add_order(itch::be16(msg.stock_locate),
              itch::be64(msg.order_ref),
              static_cast<char>(msg.side),
              itch::be32(msg.shares),
              itch::be32(msg.price));
}

void OrderBook::on_add_order_mpid(const itch::AddOrderMPID& msg) noexcept {
    ++msgs_processed_;
    add_order(itch::be16(msg.stock_locate),
              itch::be64(msg.order_ref),
              static_cast<char>(msg.side),
              itch::be32(msg.shares),
              itch::be32(msg.price));
}

void OrderBook::on_order_executed(const itch::OrderExecuted& msg) noexcept {
    ++msgs_processed_;
    reduce_order(itch::be64(msg.order_ref), itch::be32(msg.executed_shares));
}

void OrderBook::on_order_executed_with_price(const itch::OrderExecutedWithPrice& msg) noexcept {
    ++msgs_processed_;
    reduce_order(itch::be64(msg.order_ref), itch::be32(msg.executed_shares));
}

void OrderBook::on_order_cancel(const itch::OrderCancel& msg) noexcept {
    ++msgs_processed_;
    reduce_order(itch::be64(msg.order_ref), itch::be32(msg.cancelled_shares));
}

void OrderBook::on_order_delete(const itch::OrderDelete& msg) noexcept {
    ++msgs_processed_;
    delete_order(itch::be64(msg.order_ref));
}

void OrderBook::on_order_replace(const itch::OrderReplace& msg) noexcept {
    ++msgs_processed_;
    const uint64_t orig_ref = itch::be64(msg.orig_order_ref);
    const uint64_t new_ref  = itch::be64(msg.new_order_ref);
    const uint32_t shares   = itch::be32(msg.shares);
    const uint32_t price    = itch::be32(msg.price);

    auto it = orders_.find(orig_ref);
    if (__builtin_expect(it == orders_.end(), 0)) return;
    const uint16_t locate = it->second.stock_locate;
    const char side       = it->second.side;
    delete_order(orig_ref);
    add_order(locate, new_ref, side, shares, price);
}

void OrderBook::on_system_event(const itch::SystemEvent& msg) noexcept {
    ++msgs_processed_;
    (void)msg;
}

} // namespace book
