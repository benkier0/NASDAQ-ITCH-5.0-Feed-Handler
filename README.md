# NASDAQ ITCH 5.0 Feed Handler

Low-latency market data handler in C++20. Processes NASDAQ ITCH 5.0 binary messages arriving over UDP multicast and maintains a full L2 order book in real time.

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run the benchmark:
```bash
./build/bench --msgs 2000000 --warmup 200000
```

Run against a live feed (Linux):
```bash
./build/feed_handler -g 233.54.12.111 -p 15000 -i eth0 -c 3
```

## Benchmark

2M messages, 200k warmup, Apple Silicon (macOS), no hugepages, no `SCHED_FIFO`. Full output in `results/bench_no_affinity.txt`.

| Metric | Value |
|---|---|
| Throughput | 13.45 M msgs/sec |
| Mean latency | 69 ns |
| p50 | 96 ns |
| p95 | 96 ns |
| p99 | 96 ns |
| p99.9 | 384 ns |
| Min | 46 ns |
| Max | 1059 ns |

On an isolated Linux core with `SCHED_FIFO` and hugepages expect lower tail latency and higher throughput. Use `scripts/bench_compare.sh` to capture pinned vs unpinned numbers side by side.

## Design notes

**Zero-copy parsing.** ITCH 5.0 messages are packed big-endian structs matching the spec byte-for-byte. The parser casts the raw socket buffer directly to the right struct type and passes a const reference to the handler -- no intermediate copy. Sizes are verified with `static_assert` at compile time so spec drift gets caught immediately. Byte-swapping happens at the handler boundary via `__builtin_bswap*`, which compiles to a single `bswap` instruction.

**Order book layout.** Stock locate codes (the 16-bit integer NASDAQ assigns each symbol) run 0-8000, so they index directly into an array for O(1) book lookup. Bids and asks are `std::unordered_map<uint32_t, PriceLevel>` for O(1) insert/erase vs the O(log n) of `std::map`. Best-bid and best-ask do a linear scan, but they are never called on the hot receive path. A single global `unordered_map<ref, Order>` holds every live order so cancel/execute/replace each need exactly one hash lookup.

**Slab allocator.** Orders live in a pre-allocated contiguous array. The free list is intrusive -- unused slots store the next-free pointer in the same memory that would otherwise hold the object. Acquire and release are two pointer ops, no syscalls.

**MoldUDP64.** NASDAQ wraps ITCH in MoldUDP64 which adds a session ID, sequence number, and per-message length prefix. The feed handler tracks `expected_seq` across every received message; when an incoming sequence number doesn't match, it logs the gap (expected, actual, and count of lost messages) and increments a gap counter reported every million packets. Gap-fill via retransmit request is not implemented.

**Threading.** Everything runs on a single thread pinned to an isolated core via `sched_setaffinity`. `SCHED_FIFO` prevents preemption by lower-priority work. The `SPSCRingBuffer` is there if you want to hand decoded messages to a separate strategy thread without a mutex on the critical path.

**Socket tuning.** `SO_RCVBUF` at 16 MB gives the kernel enough headroom to absorb bursts without dropping. `SO_BUSY_POLL` makes the kernel spin in the NIC's receive queue rather than sleeping between interrupts -- trades CPU for latency, only makes sense on an isolated core. Interrupt coalescing (`ethtool -C rx-usecs 0`) should also be disabled on the NIC but that's a system config step. The `-i` flag takes either a dotted-decimal IP or an interface name; the socket layer resolves names via `getifaddrs`.

## Things I'd add for production

- Gap-fill retransmit against an upstream SoupBinTCP session (detection is in place; recovery is not)
- Replace `unordered_map` price levels with a flat sorted array or Fenwick tree for deep books
- Per-symbol statistics and stale quote detection
- Structured logging to a lock-free ring rather than `printf`
- Hugepages (`mmap(MAP_HUGETLB)`) for the order table to cut TLB misses at ~500K live orders
