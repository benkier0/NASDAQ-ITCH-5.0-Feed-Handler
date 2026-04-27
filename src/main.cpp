#include "../include/itch/parser.hpp"
#include "../include/book/order_book.hpp"
#include "../include/net/udp_multicast.hpp"
#include "../include/net/moldudp64.hpp"
#include "../include/util/cpu_affinity.hpp"
#include "../include/util/stats.hpp"

#include <cstdio>
#include <cinttypes>
#include <cstdlib>
#include <csignal>
#include <atomic>
#include <string>
#include <sys/socket.h>

struct FeedHandler {
    book::OrderBook        book;
    util::LatencyHistogram hist;

    void on_add_order(const itch::AddOrder& m)                               noexcept { book.on_add_order(m); }
    void on_add_order_mpid(const itch::AddOrderMPID& m)                      noexcept { book.on_add_order_mpid(m); }
    void on_order_executed(const itch::OrderExecuted& m)                     noexcept { book.on_order_executed(m); }
    void on_order_executed_with_price(const itch::OrderExecutedWithPrice& m) noexcept { book.on_order_executed_with_price(m); }
    void on_order_cancel(const itch::OrderCancel& m)                         noexcept { book.on_order_cancel(m); }
    void on_order_delete(const itch::OrderDelete& m)                         noexcept { book.on_order_delete(m); }
    void on_order_replace(const itch::OrderReplace& m)                       noexcept { book.on_order_replace(m); }
    void on_system_event(const itch::SystemEvent& m)                         noexcept { book.on_system_event(m); }
};

static std::atomic<bool> g_running{true};

static void on_signal(int) { g_running.store(false, std::memory_order_relaxed); }

static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s -g <mcast-group> -p <port> [-i <iface-or-ip>] [-c <core>]\n",
        prog);
}

int main(int argc, char* argv[]) {
    net::MulticastConfig cfg;
    cfg.interface_ip = "0.0.0.0";
    cfg.port         = 15000;
    int core = 0;

    for (int i = 1; i < argc; ++i) {
        std::string opt = argv[i];
        if (i + 1 >= argc) { print_usage(argv[0]); return 1; }
        if      (opt == "-g") { cfg.group        = argv[++i]; }
        else if (opt == "-p") { cfg.port         = static_cast<uint16_t>(std::atoi(argv[++i])); }
        else if (opt == "-i") { cfg.interface_ip = argv[++i]; }
        else if (opt == "-c") { core             = std::atoi(argv[++i]); }
        else                  { print_usage(argv[0]); return 1; }
    }

    if (cfg.group.empty()) { print_usage(argv[0]); return 1; }

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    util::pin_to_core(core);
    util::set_realtime(50);

    int sock = net::open_multicast_socket(cfg);
    std::printf("Joined %s:%u on core %d\n", cfg.group.c_str(), cfg.port, core);

    FeedHandler handler;
    alignas(64) uint8_t pkt[65536];
    uint64_t pkt_count = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        const ssize_t n = ::recvfrom(sock, pkt, sizeof(pkt), 0, nullptr, nullptr);
        if (__builtin_expect(n <= 0, 0)) continue;

        const uint64_t t0 = util::ScopedTimer::now_ns();

        net::demux(pkt, static_cast<std::size_t>(n),
            [&](const uint8_t* buf, uint16_t len, uint64_t /*seq*/) {
                (void)itch::dispatch(buf, len, handler);
            });

        handler.hist.record(util::ScopedTimer::now_ns() - t0);

        if (++pkt_count % 1'000'000 == 0) {
            std::printf("pkt=%" PRIu64 "  msgs=%" PRIu64 "\n",
                pkt_count, handler.book.msgs_processed());
            handler.hist.print_report("pkt-latency");
        }
    }

    handler.hist.print_report("final");
    net::close_multicast_socket(sock, cfg);
    return 0;
}
