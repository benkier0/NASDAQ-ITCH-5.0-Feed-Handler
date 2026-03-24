#include "../../include/book/order_book.hpp"
#include "../../include/itch/messages.hpp"
#include <cstring>

namespace book {

OrderBook::OrderBook() {
    for (auto& bk : books_)
        bk.orders.reserve(4096);
    ref_to_locate_.reserve(1 << 20);
}

void OrderBook::add_order(uint16_t locate, uint64_t ref,
                          char side, uint32_t shares, uint32_t price) noexcept {
    auto& bk = books_[locate];
    bk.orders.emplace(ref, Order{ref, price, shares, locate, side});
    ref_to_locate_.emplace(ref, locate);

    if (side == 'B') {
        auto& level = bk.bids[price];
        level.total_shares += shares;
        ++level.order_count;
    } else {
        auto& level = bk.asks[price];
        level.total_shares += shares;
        ++level.order_count;
    }
}

void OrderBook::reduce_order(uint64_t ref, uint32_t shares) noexcept {
    auto loc_it = ref_to_locate_.find(ref);
    if (__builtin_expect(loc_it == ref_to_locate_.end(), 0)) return;

    auto& bk = books_[loc_it->second];
    auto ord_it = bk.orders.find(ref);
    if (__builtin_expect(ord_it == bk.orders.end(), 0)) return;

    Order& ord = ord_it->second;
    const uint32_t remove = (shares > ord.shares) ? ord.shares : shares;
    ord.shares -= remove;

    auto reduce_level = [&](auto& level_map) {
        auto it = level_map.find(ord.price);
        if (it == level_map.end()) return;
        it->second.total_shares -= remove;
        if (ord.shares == 0) {
            --it->second.order_count;
            if (it->second.order_count == 0) level_map.erase(it);
        }
    };

    if (ord.side == 'B') reduce_level(bk.bids);
    else                  reduce_level(bk.asks);

    if (ord.shares == 0) {
        bk.orders.erase(ord_it);
        ref_to_locate_.erase(loc_it);
    }
}

void OrderBook::delete_order(uint64_t ref) noexcept {
    auto loc_it = ref_to_locate_.find(ref);
    if (__builtin_expect(loc_it == ref_to_locate_.end(), 0)) return;

    auto& bk = books_[loc_it->second];
    auto ord_it = bk.orders.find(ref);
    if (__builtin_expect(ord_it == bk.orders.end(), 0)) return;

    const Order& ord = ord_it->second;

    auto remove_from = [&](auto& level_map) {
        auto it = level_map.find(ord.price);
        if (it == level_map.end()) return;
        it->second.total_shares -= ord.shares;
        --it->second.order_count;
        if (it->second.order_count == 0) level_map.erase(it);
    };

    if (ord.side == 'B') remove_from(bk.bids);
    else                  remove_from(bk.asks);

    bk.orders.erase(ord_it);
    ref_to_locate_.erase(loc_it);
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

    auto loc_it = ref_to_locate_.find(orig_ref);
    if (__builtin_expect(loc_it == ref_to_locate_.end(), 0)) return;
    const uint16_t locate = loc_it->second;

    auto& bk = books_[locate];
    auto ord_it = bk.orders.find(orig_ref);
    if (__builtin_expect(ord_it == bk.orders.end(), 0)) return;

    const char side = ord_it->second.side;
    delete_order(orig_ref);
    add_order(locate, new_ref, side, shares, price);
}

void OrderBook::on_system_event(const itch::SystemEvent& msg) noexcept {
    ++msgs_processed_;
    (void)msg;
}

} // namespace book
