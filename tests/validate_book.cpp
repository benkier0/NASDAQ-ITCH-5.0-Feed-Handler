#include "../include/book/order_book.hpp"
#include "../include/itch/messages.hpp"

#include <map>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include <random>

struct RefOrder {
    uint64_t ref;
    uint32_t price;
    uint32_t shares;
    uint16_t locate;
    char     side;
};

struct RefLevel {
    uint64_t total_shares{0};
    uint32_t order_count{0};
};

struct RefL2 {
    std::map<uint32_t, RefLevel> bids;
    std::map<uint32_t, RefLevel> asks;
};

class RefBook {
public:
    void on_add_order(const itch::AddOrder& m) noexcept {
        add(itch::be16(m.stock_locate), itch::be64(m.order_ref),
            static_cast<char>(m.side), itch::be32(m.shares), itch::be32(m.price));
    }
    void on_add_order_mpid(const itch::AddOrderMPID& m) noexcept {
        add(itch::be16(m.stock_locate), itch::be64(m.order_ref),
            static_cast<char>(m.side), itch::be32(m.shares), itch::be32(m.price));
    }
    void on_order_executed(const itch::OrderExecuted& m) noexcept {
        reduce(itch::be64(m.order_ref), itch::be32(m.executed_shares));
    }
    void on_order_executed_with_price(const itch::OrderExecutedWithPrice& m) noexcept {
        reduce(itch::be64(m.order_ref), itch::be32(m.executed_shares));
    }
    void on_order_cancel(const itch::OrderCancel& m) noexcept {
        reduce(itch::be64(m.order_ref), itch::be32(m.cancelled_shares));
    }
    void on_order_delete(const itch::OrderDelete& m) noexcept {
        erase(itch::be64(m.order_ref));
    }
    void on_order_replace(const itch::OrderReplace& m) noexcept {
        const uint64_t orig = itch::be64(m.orig_order_ref);
        auto it = orders_.find(orig);
        if (it == orders_.end()) return;
        const uint16_t loc  = it->second.locate;
        const char     side = it->second.side;
        erase(orig);
        add(loc, itch::be64(m.new_order_ref), side,
            itch::be32(m.shares), itch::be32(m.price));
    }
    void on_system_event(const itch::SystemEvent&) noexcept {}

    const RefL2* book(uint16_t locate) const {
        auto it = books_.find(locate);
        return (it != books_.end()) ? &it->second : nullptr;
    }

private:
    void add(uint16_t loc, uint64_t ref, char side, uint32_t shares, uint32_t price) {
        orders_.emplace(ref, RefOrder{ref, price, shares, loc, side});
        auto& bk = books_[loc];
        auto& lm = (side == 'B') ? bk.bids : bk.asks;
        auto& lv = lm[price];
        lv.total_shares += shares;
        ++lv.order_count;
    }

    void reduce(uint64_t ref, uint32_t shares) {
        auto it = orders_.find(ref);
        if (it == orders_.end()) return;
        RefOrder& o = it->second;
        const uint32_t rm = (shares > o.shares) ? o.shares : shares;
        o.shares -= rm;
        auto bit = books_.find(o.locate);
        if (bit != books_.end()) {
            auto& lm = (o.side == 'B') ? bit->second.bids : bit->second.asks;
            auto li = lm.find(o.price);
            if (li != lm.end()) {
                li->second.total_shares -= rm;
                if (o.shares == 0) {
                    --li->second.order_count;
                    if (li->second.order_count == 0) lm.erase(li);
                }
            }
        }
        if (o.shares == 0) orders_.erase(it);
    }

    void erase(uint64_t ref) {
        auto it = orders_.find(ref);
        if (it == orders_.end()) return;
        const RefOrder& o = it->second;
        auto bit = books_.find(o.locate);
        if (bit != books_.end()) {
            auto& lm = (o.side == 'B') ? bit->second.bids : bit->second.asks;
            auto li = lm.find(o.price);
            if (li != lm.end()) {
                li->second.total_shares -= o.shares;
                --li->second.order_count;
                if (li->second.order_count == 0) lm.erase(li);
            }
        }
        orders_.erase(it);
    }

    std::unordered_map<uint64_t, RefOrder> orders_;
    std::unordered_map<uint16_t, RefL2>    books_;
};

static int g_failures = 0;

static void check_u64(uint64_t prod, uint64_t ref,
                      const char* label, uint16_t loc, uint32_t px) {
    if (prod != ref) {
        std::fprintf(stderr,
            "FAIL %s locate=%u price=%u  prod=%" PRIu64 "  ref=%" PRIu64 "\n",
            label, loc, px, prod, ref);
        ++g_failures;
    }
}

static void require_bid(const book::OrderBook& prod, uint16_t loc, uint32_t px,
                        uint64_t want_shares, uint32_t want_count) {
    const auto& pb = prod.book(loc);
    auto it = pb.bids.find(px);
    if (it == pb.bids.end()) {
        std::fprintf(stderr, "FAIL: bid not found loc=%u px=%u\n", loc, px);
        ++g_failures;
        return;
    }
    check_u64(it->second.total_shares, want_shares, "bid.total_shares", loc, px);
    check_u64(it->second.order_count,  want_count,  "bid.order_count",  loc, px);
}

static void require_ask(const book::OrderBook& prod, uint16_t loc, uint32_t px,
                        uint64_t want_shares, uint32_t want_count) {
    const auto& pb = prod.book(loc);
    auto it = pb.asks.find(px);
    if (it == pb.asks.end()) {
        std::fprintf(stderr, "FAIL: ask not found loc=%u px=%u\n", loc, px);
        ++g_failures;
        return;
    }
    check_u64(it->second.total_shares, want_shares, "ask.total_shares", loc, px);
    check_u64(it->second.order_count,  want_count,  "ask.order_count",  loc, px);
}

static void require_bid_absent(const book::OrderBook& prod, uint16_t loc, uint32_t px) {
    if (prod.book(loc).bids.count(px) != 0) {
        std::fprintf(stderr, "FAIL: bid should be absent loc=%u px=%u\n", loc, px);
        ++g_failures;
    }
}

static void require_ask_absent(const book::OrderBook& prod, uint16_t loc, uint32_t px) {
    if (prod.book(loc).asks.count(px) != 0) {
        std::fprintf(stderr, "FAIL: ask should be absent loc=%u px=%u\n", loc, px);
        ++g_failures;
    }
}

static void compare_books(const book::OrderBook& prod, const RefBook& ref) {
    for (uint16_t loc = 0; loc < book::MAX_STOCKS; ++loc) {
        const auto& pb = prod.book(loc);
        const auto* rb = ref.book(loc);

        for (const auto& [px, lv] : pb.bids) {
            uint64_t rs = 0;
            if (rb) {
                auto it = rb->bids.find(px);
                if (it != rb->bids.end()) rs = it->second.total_shares;
            }
            check_u64(lv.total_shares, rs, "bid.shares", loc, px);
        }
        if (rb) {
            for (const auto& [px, lv] : rb->bids) {
                auto it = pb.bids.find(px);
                uint64_t ps = (it != pb.bids.end()) ? it->second.total_shares : 0;
                check_u64(ps, lv.total_shares, "bid.shares(ref)", loc, px);
            }
            for (const auto& [px, lv] : rb->asks) {
                auto it = pb.asks.find(px);
                uint64_t ps = (it != pb.asks.end()) ? it->second.total_shares : 0;
                check_u64(ps, lv.total_shares, "ask.shares(ref)", loc, px);
            }
        }
        for (const auto& [px, lv] : pb.asks) {
            uint64_t rs = 0;
            if (rb) {
                auto it = rb->asks.find(px);
                if (it != rb->asks.end()) rs = it->second.total_shares;
            }
            check_u64(lv.total_shares, rs, "ask.shares", loc, px);
        }
    }
}

static itch::AddOrder make_add(uint64_t ref, char side,
                               uint32_t shares, uint32_t price, uint16_t loc) {
    itch::AddOrder m{};
    m.msg_type     = itch::MSG_ADD_ORDER;
    m.stock_locate = __builtin_bswap16(loc);
    m.order_ref    = __builtin_bswap64(ref);
    m.side         = static_cast<uint8_t>(side);
    m.shares       = __builtin_bswap32(shares);
    m.price        = __builtin_bswap32(price);
    return m;
}

static itch::OrderCancel make_cancel(uint64_t ref, uint32_t shares, uint16_t loc) {
    itch::OrderCancel m{};
    m.msg_type        = itch::MSG_ORDER_CANCEL;
    m.stock_locate    = __builtin_bswap16(loc);
    m.order_ref       = __builtin_bswap64(ref);
    m.cancelled_shares = __builtin_bswap32(shares);
    return m;
}

static itch::OrderExecuted make_execute(uint64_t ref, uint32_t shares, uint16_t loc) {
    itch::OrderExecuted m{};
    m.msg_type       = itch::MSG_ORDER_EXECUTED;
    m.stock_locate   = __builtin_bswap16(loc);
    m.order_ref      = __builtin_bswap64(ref);
    m.executed_shares = __builtin_bswap32(shares);
    return m;
}

static itch::OrderDelete make_delete(uint64_t ref, uint16_t loc) {
    itch::OrderDelete m{};
    m.msg_type     = itch::MSG_ORDER_DELETE;
    m.stock_locate = __builtin_bswap16(loc);
    m.order_ref    = __builtin_bswap64(ref);
    return m;
}

static itch::OrderReplace make_replace(uint64_t orig, uint64_t rep,
                                       uint32_t shares, uint32_t price, uint16_t loc) {
    itch::OrderReplace m{};
    m.msg_type       = itch::MSG_ORDER_REPLACE;
    m.stock_locate   = __builtin_bswap16(loc);
    m.orig_order_ref = __builtin_bswap64(orig);
    m.new_order_ref  = __builtin_bswap64(rep);
    m.shares         = __builtin_bswap32(shares);
    m.price          = __builtin_bswap32(price);
    return m;
}

static void test_known_sequence() {
    book::OrderBook prod;
    RefBook ref;

    auto a1 = make_add(1, 'B', 100, 1000, 0);
    prod.on_add_order(a1); ref.on_add_order(a1);

    auto a2 = make_add(2, 'B', 200, 1000, 0);
    prod.on_add_order(a2); ref.on_add_order(a2);

    require_bid(prod, 0, 1000, 300, 2);

    auto c1 = make_cancel(1, 50, 0);
    prod.on_order_cancel(c1); ref.on_order_cancel(c1);

    require_bid(prod, 0, 1000, 250, 2);

    auto e2 = make_execute(2, 200, 0);
    prod.on_order_executed(e2); ref.on_order_executed(e2);

    require_bid(prod, 0, 1000, 50, 1);

    auto d1 = make_delete(1, 0);
    prod.on_order_delete(d1); ref.on_order_delete(d1);

    require_bid_absent(prod, 0, 1000);

    auto a3 = make_add(3, 'S', 500, 2000, 0);
    prod.on_add_order(a3); ref.on_add_order(a3);

    auto r3 = make_replace(3, 5, 400, 1900, 0);
    prod.on_order_replace(r3); ref.on_order_replace(r3);

    require_ask_absent(prod, 0, 2000);
    require_ask(prod, 0, 1900, 400, 1);

    compare_books(prod, ref);
    std::printf("known-sequence: %s\n", g_failures == 0 ? "PASS" : "FAIL");
}

static void test_random_sequence(uint64_t total, uint64_t seed) {
    book::OrderBook prod;
    RefBook ref;

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> price_d(100'0000, 500'0000);
    std::uniform_int_distribution<uint32_t> shares_d(1, 1000);
    std::uniform_int_distribution<int>      side_d(0, 1);
    std::uniform_int_distribution<int>      action_d(0, 3);
    std::uniform_int_distribution<uint16_t> locate_d(0, 15);

    struct Live { uint64_t ref; uint32_t shares; uint16_t locate; };
    std::vector<Live> live;
    live.reserve(256 * 1024);
    uint64_t next_ref = 1;

    for (uint64_t i = 0; i < total; ++i) {
        int action = live.empty() ? 0 : action_d(rng);

        if (action == 0 || live.size() > 500'000) {
            const uint16_t loc  = locate_d(rng);
            const char     side = (side_d(rng) == 0) ? 'B' : 'S';
            const uint32_t sh   = shares_d(rng);
            const uint32_t px   = price_d(rng);
            auto m = make_add(next_ref, side, sh, px, loc);
            prod.on_add_order(m);
            ref.on_add_order(m);
            live.push_back({next_ref++, sh, loc});
        } else {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t idx = pick(rng);
            Live& lo = live[idx];

            if (action == 1) {
                uint32_t sh = std::min(lo.shares, shares_d(rng));
                if (sh == 0) sh = 1;
                auto m = make_cancel(lo.ref, sh, lo.locate);
                prod.on_order_cancel(m);
                ref.on_order_cancel(m);
                lo.shares -= sh;
                if (lo.shares == 0)
                    live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
            } else if (action == 2) {
                auto m = make_execute(lo.ref, lo.shares, lo.locate);
                prod.on_order_executed(m);
                ref.on_order_executed(m);
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
            } else {
                const uint32_t sh = shares_d(rng);
                const uint32_t px = price_d(rng);
                auto m = make_replace(lo.ref, next_ref, sh, px, lo.locate);
                prod.on_order_replace(m);
                ref.on_order_replace(m);
                lo.ref    = next_ref++;
                lo.shares = sh;
            }
        }
    }

    compare_books(prod, ref);
    std::printf("random-sequence (n=%" PRIu64 " seed=%" PRIu64 "): %s\n",
                total, seed, g_failures == 0 ? "PASS" : "FAIL");
}

int main() {
    test_known_sequence();
    test_random_sequence(500'000, 42);
    test_random_sequence(500'000, 0xdeadbeef);
    test_random_sequence(500'000, 12345678);

    if (g_failures > 0) {
        std::fprintf(stderr, "%d divergence(s) detected\n", g_failures);
        return 1;
    }
    std::printf("All validation tests passed.\n");
    return 0;
}
