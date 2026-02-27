#pragma once
#include <cstdint>
#include <cstddef>

// MoldUDP64 framing (NASDAQ's UDP transport layer for ITCH).
//
// Packet layout:
//   Session[10]  Sequence[8 BE]  MsgCount[2 BE]
//   for each message: Length[2 BE]  Data[n]
//
// MsgCount 0xFFFF = heartbeat, 0 = end-of-session.

namespace net {

#pragma pack(push, 1)
struct MoldHeader {
    char     session[10];
    uint64_t seq_number;    // big-endian
    uint16_t msg_count;     // big-endian
};
static_assert(sizeof(MoldHeader) == 20);
#pragma pack(pop)

inline constexpr uint16_t MOLD_HEARTBEAT   = 0xFFFFU;
inline constexpr uint16_t MOLD_END_SESSION = 0x0000U;

template<typename Callback>
int demux(const uint8_t* pkt, std::size_t pkt_len, Callback&& cb) noexcept {
    if (pkt_len < sizeof(MoldHeader)) return -1;

    const auto* hdr = reinterpret_cast<const MoldHeader*>(pkt);
    const uint16_t count    = __builtin_bswap16(hdr->msg_count);
    const uint64_t base_seq = __builtin_bswap64(hdr->seq_number);

    if (count == MOLD_HEARTBEAT || count == MOLD_END_SESSION) return 0;

    const uint8_t* cursor = pkt + sizeof(MoldHeader);
    const uint8_t* end    = pkt + pkt_len;

    for (uint16_t i = 0; i < count; ++i) {
        if (cursor + 2 > end) return -1;
        const uint16_t msg_len = __builtin_bswap16(
            *reinterpret_cast<const uint16_t*>(cursor));
        cursor += 2;
        if (cursor + msg_len > end) return -1;
        cb(cursor, msg_len, base_seq + i);
        cursor += msg_len;
    }
    return count;
}

} // namespace net
