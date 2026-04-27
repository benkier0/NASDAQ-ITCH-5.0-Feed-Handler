#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cinttypes>
#include <cmath>
#include <time.h>

namespace util {

class LatencyHistogram {
    static constexpr int NUM_BUCKETS = 64;

    alignas(64) std::array<std::atomic<uint64_t>, NUM_BUCKETS> buckets_{};
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> total_ns_{0};
    std::atomic<uint64_t> min_ns_{UINT64_MAX};
    std::atomic<uint64_t> max_ns_{0};

public:
    void record(uint64_t ns) noexcept {
        count_.fetch_add(1, std::memory_order_relaxed);
        total_ns_.fetch_add(ns, std::memory_order_relaxed);

        uint64_t cur = min_ns_.load(std::memory_order_relaxed);
        while (ns < cur && !min_ns_.compare_exchange_weak(cur, ns,
                std::memory_order_relaxed)) {}

        cur = max_ns_.load(std::memory_order_relaxed);
        while (ns > cur && !max_ns_.compare_exchange_weak(cur, ns,
                std::memory_order_relaxed)) {}

        const int bucket = 63 - __builtin_clzll(ns | 1ULL);
        buckets_[bucket].fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t percentile(double pct) const noexcept {
        const uint64_t total = count_.load(std::memory_order_acquire);
        if (total == 0) return 0;
        const auto target = static_cast<uint64_t>(
            std::ceil(pct / 100.0 * static_cast<double>(total)));
        uint64_t cumulative = 0;
        for (int k = 0; k < NUM_BUCKETS; ++k) {
            cumulative += buckets_[k].load(std::memory_order_relaxed);
            if (cumulative >= target)
                return k == 0 ? 1ULL : (3ULL << (k - 1));
        }
        return max_ns_.load(std::memory_order_relaxed);
    }

    void print_report(const char* label = "latency") const noexcept {
        const uint64_t n = count_.load(std::memory_order_acquire);
        if (n == 0) { std::printf("[%s] no samples\n", label); return; }
        const double mean = static_cast<double>(
            total_ns_.load(std::memory_order_relaxed)) / static_cast<double>(n);
        std::printf("[%s] n=%" PRIu64 "  mean=%.1f ns"
                    "  p50=%" PRIu64 " ns  p95=%" PRIu64 " ns"
                    "  p99=%" PRIu64 " ns  p999=%" PRIu64 " ns"
                    "  min=%" PRIu64 " ns  max=%" PRIu64 " ns\n",
            label, n, mean,
            percentile(50), percentile(95),
            percentile(99), percentile(99.9),
            min_ns_.load(std::memory_order_relaxed),
            max_ns_.load(std::memory_order_relaxed));
    }

    void reset() noexcept {
        for (auto& b : buckets_) b.store(0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
        total_ns_.store(0, std::memory_order_relaxed);
        min_ns_.store(UINT64_MAX, std::memory_order_relaxed);
        max_ns_.store(0, std::memory_order_relaxed);
    }
};

struct ScopedTimer {
    LatencyHistogram& hist;
    uint64_t          start;

    explicit ScopedTimer(LatencyHistogram& h) noexcept
        : hist(h), start(now_ns()) {}

    ~ScopedTimer() noexcept { hist.record(now_ns() - start); }

    static uint64_t now_ns() noexcept {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
               static_cast<uint64_t>(ts.tv_nsec);
    }
};

} // namespace util
