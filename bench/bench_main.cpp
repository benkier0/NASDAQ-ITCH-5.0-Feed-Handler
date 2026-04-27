#include "../include/itch/parser.hpp"
#include "../include/book/order_book.hpp"
#include "../include/util/stats.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>
#include <chrono>
#include <cinttypes>

static void fill_add(itch::AddOrder& m, uint64_t ref, char side,
                     uint32_t shares, uint32_t price, uint16_t locate) {
    m.msg_type        = itch::MSG_ADD_ORDER;
    m.stock_locate    = __builtin_bswap16(locate);
    m.tracking_number = 0;
    std::memset(m.timestamp, 0, 6);
    m.order_ref       = __builtin_bswap64(ref);
    m.side            = static_cast<uint8_t>(side);
    m.shares          = __builtin_bswap32(shares);
    std::memcpy(m.stock, "AAPL    ", 8);
    m.price           = __builtin_bswap32(price);
}

static void fill_cancel(itch::OrderCancel& m, uint64_t ref,
                        uint32_t cancel_shares, uint16_t locate) {
    m.msg_type         = itch::MSG_ORDER_CANCEL;
    m.stock_locate     = __builtin_bswap16(locate);
    m.tracking_number  = 0;
    std::memset(m.timestamp, 0, 6);
    m.order_ref        = __builtin_bswap64(ref);
    m.cancelled_shares = __builtin_bswap32(cancel_shares);
}

static void fill_execute(itch::OrderExecuted& m, uint64_t ref,
                         uint32_t exec_shares, uint16_t locate) {
    m.msg_type        = itch::MSG_ORDER_EXECUTED;
    m.stock_locate    = __builtin_bswap16(locate);
    m.tracking_number = 0;
    std::memset(m.timestamp, 0, 6);
    m.order_ref       = __builtin_bswap64(ref);
    m.executed_shares = __builtin_bswap32(exec_shares);
    m.match_number    = 0;
}

static void fill_replace(itch::OrderReplace& m, uint64_t orig, uint64_t replacement,
                         uint32_t shares, uint32_t price, uint16_t locate) {
    m.msg_type        = itch::MSG_ORDER_REPLACE;
    m.stock_locate    = __builtin_bswap16(locate);
    m.tracking_number = 0;
    std::memset(m.timestamp, 0, 6);
    m.orig_order_ref  = __builtin_bswap64(orig);
    m.new_order_ref   = __builtin_bswap64(replacement);
    m.shares          = __builtin_bswap32(shares);
    m.price           = __builtin_bswap32(price);
}

struct Packet {
    uint8_t  buf[64];
    uint16_t len;
};

struct BenchHandler {
    book::OrderBook        book;
    util::LatencyHistogram hist;

    void on_add_order(const itch::AddOrder& m) noexcept { book.on_add_order(m); }
    void on_add_order_mpid(const itch::AddOrderMPID& m) noexcept { book.on_add_order_mpid(m); }
    void on_order_executed(const itch::OrderExecuted& m) noexcept { book.on_order_executed(m); }
    void on_order_executed_with_price(const itch::OrderExecutedWithPrice& m) noexcept { book.on_order_executed_with_price(m); }
    void on_order_cancel(const itch::OrderCancel& m) noexcept { book.on_order_cancel(m); }
    void on_order_delete(const itch::OrderDelete& m) noexcept { book.on_order_delete(m); }
    void on_order_replace(const itch::OrderReplace& m) noexcept { book.on_order_replace(m); }
    void on_system_event(const itch::SystemEvent& m) noexcept { book.on_system_event(m); }
};

static std::vector<Packet> generate_sequence(uint64_t total_msgs, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> price_dist(100'0000, 500'0000);
    std::uniform_int_distribution<uint32_t> shares_dist(1, 1000);
    std::uniform_int_distribution<int>      side_dist(0, 1);
    std::uniform_int_distribution<int>      action_dist(0, 3);
    std::uniform_int_distribution<uint16_t> locate_dist(0, 15);

    std::vector<Packet> pkts;
    pkts.reserve(total_msgs);

    struct LiveOrder { uint64_t ref; uint32_t shares; uint16_t locate; };
    std::vector<LiveOrder> live;
    live.reserve(256 * 1024);

    uint64_t next_ref = 1;
    uint64_t adds = 0, cancels = 0, executes = 0, replaces = 0;

    for (uint64_t i = 0; i < total_msgs; ++i) {
        Packet pkt{};
        int action = live.empty() ? 0 : action_dist(rng);

        if (action == 0 || live.size() > 500'000) {
            itch::AddOrder msg{};
            const uint16_t loc  = locate_dist(rng);
            const char     side = (side_dist(rng) == 0) ? 'B' : 'S';
            const uint32_t sh   = shares_dist(rng);
            const uint32_t px   = price_dist(rng);
            fill_add(msg, next_ref, side, sh, px, loc);
            std::memcpy(pkt.buf, &msg, sizeof(msg));
            pkt.len = sizeof(msg);
            live.push_back({next_ref++, sh, loc});
            ++adds;
        } else {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t idx = pick(rng);
            LiveOrder& lo = live[idx];

            if (action == 1) {
                uint32_t cancel_sh = std::min(lo.shares, shares_dist(rng));
                if (cancel_sh == 0) cancel_sh = 1;
                itch::OrderCancel msg{};
                fill_cancel(msg, lo.ref, cancel_sh, lo.locate);
                std::memcpy(pkt.buf, &msg, sizeof(msg));
                pkt.len = sizeof(msg);
                lo.shares -= cancel_sh;
                if (lo.shares == 0)
                    live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
                ++cancels;
            } else if (action == 2) {
                itch::OrderExecuted msg{};
                fill_execute(msg, lo.ref, lo.shares, lo.locate);
                std::memcpy(pkt.buf, &msg, sizeof(msg));
                pkt.len = sizeof(msg);
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
                ++executes;
            } else {
                const uint32_t new_sh = shares_dist(rng);
                const uint32_t new_px = price_dist(rng);
                itch::OrderReplace msg{};
                fill_replace(msg, lo.ref, next_ref, new_sh, new_px, lo.locate);
                std::memcpy(pkt.buf, &msg, sizeof(msg));
                pkt.len = sizeof(msg);
                lo.ref    = next_ref++;
                lo.shares = new_sh;
                ++replaces;
            }
        }
        pkts.push_back(pkt);
    }

    std::printf("Generated %" PRIu64 " msgs: adds=%" PRIu64
                " cancels=%" PRIu64 " executes=%" PRIu64 " replaces=%" PRIu64 "\n",
                total_msgs, adds, cancels, executes, replaces);
    return pkts;
}

int main(int argc, char* argv[]) {
    uint64_t total_msgs  = 1'000'000;
    uint64_t warmup_msgs = 100'000;
    uint64_t seed        = 42;

    for (int i = 1; i < argc - 1; ++i) {
        if      (std::strcmp(argv[i], "--msgs")   == 0) total_msgs  = std::strtoull(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--warmup") == 0) warmup_msgs = std::strtoull(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--seed")   == 0) seed        = std::strtoull(argv[++i], nullptr, 10);
    }

    auto pkts = generate_sequence(total_msgs, seed);

    {
        BenchHandler warmup_handler;
        const uint64_t wn = std::min(warmup_msgs, static_cast<uint64_t>(pkts.size()));
        for (uint64_t i = 0; i < wn; ++i)
            (void)itch::dispatch(pkts[i].buf, pkts[i].len, warmup_handler);
        std::printf("Warmup: %" PRIu64 " msgs processed\n", wn);
    }

    BenchHandler handler;

    const auto wall_start = std::chrono::steady_clock::now();
    for (const auto& pkt : pkts)
        (void)itch::dispatch(pkt.buf, pkt.len, handler);
    const auto wall_end = std::chrono::steady_clock::now();

    util::LatencyHistogram per_msg_hist;
    {
        BenchHandler lat_handler;
        // Warm the lat_handler's book with the first warmup_msgs
        const uint64_t wn = std::min(warmup_msgs, static_cast<uint64_t>(pkts.size()));
        for (uint64_t i = 0; i < wn; ++i)
            (void)itch::dispatch(pkts[i].buf, pkts[i].len, lat_handler);

        constexpr uint64_t BATCH = 64;
        const uint64_t n = static_cast<uint64_t>(pkts.size());
        for (uint64_t i = 0; i + BATCH <= n; i += BATCH) {
            const uint64_t t0 = util::ScopedTimer::now_ns();
            for (uint64_t j = i; j < i + BATCH; ++j)
                (void)itch::dispatch(pkts[j].buf, pkts[j].len, lat_handler);
            const uint64_t elapsed = util::ScopedTimer::now_ns() - t0;
            per_msg_hist.record(elapsed / BATCH);
        }
    }
    const double wall_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            wall_end - wall_start).count());

    const double msgs_per_sec = static_cast<double>(pkts.size()) / (wall_ns / 1e9);
    std::printf("\n---- Benchmark Results ----\n");
    std::printf("Messages    : %" PRIu64 "\n", static_cast<uint64_t>(pkts.size()));
    std::printf("Wall time   : %.3f ms\n", wall_ns / 1e6);
    std::printf("Throughput  : %.2f M msgs/sec\n", msgs_per_sec / 1e6);
    per_msg_hist.print_report("per-msg");
    std::printf("Book msgs   : %" PRIu64 "\n", handler.book.msgs_processed());

    uint64_t total_shares = 0;
    for (uint16_t loc = 0; loc < book::MAX_STOCKS; ++loc) {
        const auto& bk = handler.book.book(loc);
        for (const auto& [px, lvl] : bk.bids) total_shares += lvl.total_shares;
        for (const auto& [px, lvl] : bk.asks) total_shares += lvl.total_shares;
    }
    std::printf("Shares remaining: %" PRIu64 "\n", total_shares);

    return 0;
}
