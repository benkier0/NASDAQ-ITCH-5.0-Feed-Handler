#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>

namespace net {

struct MulticastConfig {
    std::string group;
    std::string interface_ip;
    uint16_t    port{15000};
    int         rcvbuf_bytes{16 * 1024 * 1024};
    int         busy_poll_us{50};   // SO_BUSY_POLL; 0 to disable
};

[[nodiscard]] int open_multicast_socket(const MulticastConfig& cfg);

void close_multicast_socket(int fd, const MulticastConfig& cfg) noexcept;

} // namespace net
