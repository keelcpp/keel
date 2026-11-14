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

void sleep_ns(u64 ns) noexcept {
    if (ns == 0) return;
#if defined(__EMSCRIPTEN__)
    (void)ns;
#elif defined(_WIN32)
    const u64 target = monotonic_ns() + ns;
    for (;;) {
        const u64 now = monotonic_ns();
        if (now >= target) break;
        const u64 rem = target - now;
        if (rem >= 2000000ULL) {
            Sleep(static_cast<DWORD>(rem / 1000000ULL));
        }
    }
#else
    const u64 target = monotonic_ns() + ns;
    for (;;) {
        const u64 now = monotonic_ns();
        if (now >= target) break;
        const u64 rem = target - now;
        struct timespec ts {};
        ts.tv_sec = static_cast<time_t>(rem / 1000000000ULL);
        ts.tv_nsec = static_cast<long>(rem % 1000000000ULL);
        nanosleep(&ts, nullptr);
    }
#endif
}

} // namespace keel
