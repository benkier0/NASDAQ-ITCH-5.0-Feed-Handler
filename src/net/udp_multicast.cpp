#include "../../include/net/udp_multicast.hpp"
#include <stdexcept>
#include <string>
#include <cerrno>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#ifdef __linux__
#include <net/if.h>
#endif

namespace net {

static void throw_errno(const char* ctx) {
    throw std::runtime_error(std::string(ctx) + ": " + std::strerror(errno));
}

int open_multicast_socket(const MulticastConfig& cfg) {
    int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) throw_errno("socket");

    int reuse = 1;
    if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        throw_errno("SO_REUSEADDR");

    if (::setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                     &cfg.rcvbuf_bytes, sizeof(cfg.rcvbuf_bytes)) < 0)
        throw_errno("SO_RCVBUF");

#ifdef __linux__
    if (cfg.busy_poll_us > 0) {
        ::setsockopt(sock, SOL_SOCKET, SO_BUSY_POLL,
                     &cfg.busy_poll_us, sizeof(cfg.busy_poll_us));
    }
#endif

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(cfg.port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
        throw_errno("bind");

    struct ip_mreq mreq{};
    if (::inet_pton(AF_INET, cfg.group.c_str(), &mreq.imr_multiaddr) != 1)
        throw_errno("inet_pton(group)");
    if (cfg.interface_ip == "0.0.0.0" || cfg.interface_ip.empty()) {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, cfg.interface_ip.c_str(), &mreq.imr_interface) != 1) {
        struct ifaddrs* ifap = nullptr;
        if (::getifaddrs(&ifap) < 0) throw_errno("getifaddrs");
        bool found = false;
        for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (cfg.interface_ip == ifa->ifa_name) {
                mreq.imr_interface =
                    reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr)->sin_addr;
                found = true;
                break;
            }
        }
        ::freeifaddrs(ifap);
        if (!found)
            throw std::runtime_error("interface not found: " + cfg.interface_ip);
    }
    if (::setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
        throw_errno("IP_ADD_MEMBERSHIP");

    return sock;
}

void close_multicast_socket(int sock, const MulticastConfig& cfg) noexcept {
    struct ip_mreq mreq{};
    ::inet_pton(AF_INET, cfg.group.c_str(), &mreq.imr_multiaddr);
    ::setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
    ::close(sock);
}

} // namespace net
