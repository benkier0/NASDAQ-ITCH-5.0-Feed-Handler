#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <cstring>
#endif

namespace util {

inline void pin_to_core(int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
        throw std::runtime_error(
            std::string("pin_to_core(") + std::to_string(core_id) +
            "): " + std::strerror(errno));
    }
#else
    (void)core_id;
#endif
}

// Tries SCHED_FIFO; non-fatal if the process lacks CAP_SYS_NICE.
inline void set_realtime(int priority = 50) {
#ifdef __linux__
    struct sched_param sp{};
    sp.sched_priority = priority;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
#else
    (void)priority;
#endif
}

} // namespace util
