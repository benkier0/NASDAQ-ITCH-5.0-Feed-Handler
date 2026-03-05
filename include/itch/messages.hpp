#pragma once
#include <cstdint>

namespace itch {

inline constexpr uint8_t MSG_SYSTEM_EVENT     = 'S';
inline constexpr uint8_t MSG_STOCK_DIR        = 'R';
inline constexpr uint8_t MSG_ADD_ORDER        = 'A';
inline constexpr uint8_t MSG_ADD_ORDER_MPID   = 'F';
inline constexpr uint8_t MSG_ORDER_EXECUTED   = 'E';
inline constexpr uint8_t MSG_ORDER_EXEC_PRICE = 'C';
inline constexpr uint8_t MSG_ORDER_CANCEL     = 'X';
inline constexpr uint8_t MSG_ORDER_DELETE     = 'D';
inline constexpr uint8_t MSG_ORDER_REPLACE    = 'U';
inline constexpr uint8_t MSG_TRADE            = 'P';
inline constexpr uint8_t MSG_CROSS_TRADE      = 'Q';
inline constexpr uint8_t MSG_BROKEN_TRADE     = 'B';
inline constexpr uint8_t MSG_NOII             = 'I';

inline uint16_t be16(uint16_t v) noexcept { return __builtin_bswap16(v); }
inline uint32_t be32(uint32_t v) noexcept { return __builtin_bswap32(v); }
inline uint64_t be64(uint64_t v) noexcept { return __builtin_bswap64(v); }

// ITCH timestamps are 48-bit big-endian nanoseconds past midnight
inline uint64_t decode_ts(const uint8_t ts[6]) noexcept {
    return (uint64_t(ts[0]) << 40) | (uint64_t(ts[1]) << 32) |
           (uint64_t(ts[2]) << 24) | (uint64_t(ts[3]) << 16) |
           (uint64_t(ts[4]) <<  8) |  uint64_t(ts[5]);
}

#pragma pack(push, 1)

struct AddOrder {                   // type 'A', 36 bytes
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_ref;
    uint8_t  side;                  // 'B' or 'S'
    uint32_t shares;
    char     stock[8];              // ASCII, right-padded with spaces
    uint32_t price;                 // 4 implied decimal places
};
static_assert(sizeof(AddOrder) == 36);

struct AddOrderMPID {               // type 'F', 40 bytes
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_ref;
    uint8_t  side;
    uint32_t shares;
    char     stock[8];
    uint32_t price;
    char     attribution[4];
};
static_assert(sizeof(AddOrderMPID) == 40);

struct OrderExecuted {              // type 'E', 31 bytes
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_ref;
    uint32_t executed_shares;
    uint64_t match_number;
};
static_assert(sizeof(OrderExecuted) == 31);

struct OrderExecutedWithPrice {     // type 'C', 36 bytes
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_ref;
    uint32_t executed_shares;
    uint64_t match_number;
    uint8_t  printable;
    uint32_t execution_price;
};
static_assert(sizeof(OrderExecutedWithPrice) == 36);

struct OrderCancel {                // type 'X', 23 bytes
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_ref;
    uint32_t cancelled_shares;
};
static_assert(sizeof(OrderCancel) == 23);

struct OrderDelete {                // type 'D', 19 bytes
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_ref;
};
static_assert(sizeof(OrderDelete) == 19);

struct OrderReplace {               // type 'U', 35 bytes
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t orig_order_ref;
    uint64_t new_order_ref;
    uint32_t shares;
    uint32_t price;
};
static_assert(sizeof(OrderReplace) == 35);

struct SystemEvent {                // type 'S', 12 bytes
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint8_t  event_code;            // 'O' open, 'S' start, 'Q' end, 'M' close
};
static_assert(sizeof(SystemEvent) == 12);

#pragma pack(pop)

} // namespace itch
