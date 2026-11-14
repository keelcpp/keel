#include "keel/time.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace keel {

u64 monotonic_ns() noexcept {
#if defined(_WIN32)
    static const u64 s_freq = []() noexcept -> u64 {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? static_cast<u64>(f.QuadPart) : 10000000ULL;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const u64 ticks = static_cast<u64>(counter.QuadPart);
    const u64 whole_sec = ticks / s_freq;
    const u64 rem_ticks = ticks % s_freq;
    return whole_sec * 1000000000ULL + (rem_ticks * 1000000000ULL) / s_freq;
#else
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<u64>(ts.tv_sec) * 1000000000ULL + static_cast<u64>(ts.tv_nsec);
#endif
}

} // namespace keel
