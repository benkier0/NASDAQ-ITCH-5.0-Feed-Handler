#pragma once
#include <cstdint>

namespace book {

struct Order {
    uint64_t ref;
    uint32_t price;     // raw wire price (4 implied decimal places)
    uint32_t shares;
    uint16_t stock_locate;
    char     side;      // 'B' or 'S'
};

} // namespace book
