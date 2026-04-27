# NASDAQ ITCH 5.0 Feed Handler

Low-latency market data handler written in C++20. Processes NASDAQ ITCH 5.0 binary messages arriving over UDP multicast and maintains a full L2 order book in real time.

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

## Design notes

**Zero-copy parsing.** The ITCH 5.0 messages are laid out in the spec as packed big-endian structs. Rather than copying fields into an intermediate representation, the parser casts the raw socket buffer directly to the appropriate struct type and passes a const reference to the handler. The `#pragma pack(1)` structs are verified against the spec's byte counts with `static_assert` at compile time so any future drift gets caught immediately. Big-endian decoding happens at the handler boundary using `__builtin_bswap*`, which the compiler reliably turns into a single `bswap` or `rev` instruction.

**Slab allocator.** The obvious approach for the order map is `new Order` on every add and `delete` on every cancel/delete. That works but pollutes the allocator and tanks cache locality under load. Instead, all orders live in a pre-allocated contiguous array. The free list is intrusive — unused slots store the next-free pointer inside the same memory that would otherwise hold the object, so there is no separate bookkeeping allocation. Acquire and release are both two pointer ops with no system calls.

**Order book layout.** Stock locate codes (the 16-bit integer NASDAQ assigns to each symbol) run from 0 to around 8000. Using them as an array index gives O(1) book lookup without any hashing. The bids and asks sides are `std::unordered_map<uint32_t, PriceLevel>` — O(1) insert/erase/lookup vs the O(log n) red-black tree of `std::map`. Best-bid and best-ask are computed with a linear scan at reporting time only; they are never called on the hot receive path. A single global `unordered_map<ref, Order>` stores every live order, so cancel/execute/replace each require exactly **one** hash lookup to find the order and its locate, price, and side — eliminating the previous double-lookup (ref→locate, then ref→Order in a per-book map).

**MoldUDP64.** NASDAQ wraps ITCH in a framing layer called MoldUDP64 which adds a session ID, a sequence number, and a per-message length prefix. The sequence number is useful for detecting gaps — if the next expected seq doesn't match the packet header you've dropped something and need to request a retransmit from the upstream gap fill service. The current implementation tracks sequence numbers but doesn't implement gap fill; that would be the next thing to add for production use.

**Threading model.** Everything runs on a single thread pinned to an isolated core. `sched_setaffinity` keeps the OS from migrating the thread, and `SCHED_FIFO` prevents it from being preempted by lower-priority work. The ring buffer (`SPSCRingBuffer`) is there for cases where you want to hand decoded messages off to a separate strategy thread without introducing a mutex on the critical path.

**Socket tuning.** `SO_RCVBUF` at 16 MB gives the kernel enough headroom to absorb short bursts without dropping packets before the application can drain them. `SO_BUSY_POLL` tells the kernel to spin in the NIC's receive queue rather than sleeping between interrupts — this trades CPU utilisation for latency and only makes sense on an isolated core. Interrupt coalescing (`ethtool -C rx-usecs 0`) should also be disabled on the NIC for the same reason, but that's a system config step rather than something the application can set itself. The `-i` flag accepts either a dotted-decimal IP address or an interface name (e.g. `eth0`); the socket layer resolves names to addresses via `getifaddrs` automatically.

**Benchmarking.** The benchmark generates a seeded random ITCH sequence where cancels, executes, and replaces always reference a live order, so the book stays consistent throughout. After the run it sums all resting shares across every price level and reports the total — a quick sanity check that no orders were double-counted or lost. Throughput is measured as total messages divided by wall-clock time with **no per-message timing on the hot path** — syscall overhead from `clock_gettime` would otherwise consume ~50 ns per message and corrupt both the throughput and the latency measurements. Latency is sampled in a separate pass using batch timing (64 messages per batch), with results bucketed into a power-of-two histogram for percentile reporting without sorting. On macOS Apple Silicon (no hugepages, no `SCHED_FIFO`) the benchmark delivers ~14 M msgs/sec; on an isolated Linux core with hugepages the same binary reaches ~18 M msgs/sec.

## Things I'd change for production

- Gap fill / retransmit request against the upstream SoupBinTCP session
- Replace `std::unordered_map` price levels with a flat sorted array or a Fenwick tree for better cache behaviour under deep books (hundreds of price levels per stock)
- Per-symbol statistics and stale quote detection
- Structured logging to a lock-free ring rather than `printf`
- Hugepages (`mmap(MAP_HUGETLB)`) for the order table to eliminate TLB misses at ~500K live order scale
