#include "keel/time.h"

#if defined(_WIN32)
#include "win32_compat.h"
#else
#include <time.h>
#endif

namespace keel {

namespace {

#if defined(_WIN32)
[[nodiscard]] CalendarTime system_time_to_calendar(const SYSTEMTIME& st) noexcept {
    static constexpr i32 kDaysBeforeMonth[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const i32 year = as<i32>(st.wYear);
    const i32 month = clamp(as<i32>(st.wMonth), 1, 12);
    const i32 day = clamp(as<i32>(st.wDay), 1, 31);
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    const i32 doy = kDaysBeforeMonth[month - 1] + day + ((leap && month > 2) ? 1 : 0);
    return CalendarTime{
        .year = year,
        .month = month,
        .day = day,
        .hour = clamp(as<i32>(st.wHour), 0, 23),
        .minute = clamp(as<i32>(st.wMinute), 0, 59),
        .second = clamp(as<i32>(st.wSecond), 0, 59),
        .millisecond = clamp(as<i32>(st.wMilliseconds), 0, 999),
        .day_of_week = clamp(as<i32>(st.wDayOfWeek), 0, 6),
        .day_of_year = clamp(doy, 1, 366),
    };
}
#else
[[nodiscard]] CalendarTime tm_to_calendar(const struct tm& t, i32 ms) noexcept {
    return CalendarTime{
        .year = as<i32>(t.tm_year) + 1900,
        .month = clamp(as<i32>(t.tm_mon) + 1, 1, 12),
        .day = clamp(as<i32>(t.tm_mday), 1, 31),
        .hour = clamp(as<i32>(t.tm_hour), 0, 23),
        .minute = clamp(as<i32>(t.tm_min), 0, 59),
        .second = clamp(as<i32>(t.tm_sec), 0, 59),
        .millisecond = clamp(ms, 0, 999),
        .day_of_week = clamp(as<i32>(t.tm_wday), 0, 6),
        .day_of_year = clamp(as<i32>(t.tm_yday) + 1, 1, 366),
    };
}
#endif

} // namespace

u64 monotonic_ns() noexcept {
#if defined(_WIN32)
    static const u64 s_freq = []() noexcept -> u64 {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? as<u64>(f.QuadPart) : 10000000ULL;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const u64 ticks = as<u64>(counter.QuadPart);
    const u64 whole_sec = ticks / s_freq;
    const u64 rem_ticks = ticks % s_freq;
    return whole_sec * 1000000000ULL + (rem_ticks * 1000000000ULL) / s_freq;
#else
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return as<u64>(ts.tv_sec) * 1000000000ULL + as<u64>(ts.tv_nsec);
#endif
}

i64 wall_clock_ns() noexcept {
#if defined(_WIN32)
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli{};
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // Windows FILETIME epoch is 1601-01-01 in 100ns ticks; Unix epoch is 1970-01-01.
    static constexpr u64 kUnixEpochTicks = 116444736000000000ULL;
    if (uli.QuadPart < kUnixEpochTicks) {
        return -as<i64>((kUnixEpochTicks - uli.QuadPart) * 100ULL);
    }
    return as<i64>((uli.QuadPart - kUnixEpochTicks) * 100ULL);
#else
    struct timespec ts {};
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return as<i64>(ts.tv_sec) * 1000000000LL + as<i64>(ts.tv_nsec);
#endif
}

CalendarTime local_time() noexcept {
#if defined(_WIN32)
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return system_time_to_calendar(st);
#else
    struct timespec ts {};
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return {};
    }
    struct tm local_tm {};
    if (!localtime_r(&ts.tv_sec, &local_tm)) {
        return {};
    }
    return tm_to_calendar(local_tm, as<i32>(ts.tv_nsec / 1000000L));
#endif
}

CalendarTime utc_time() noexcept {
#if defined(_WIN32)
    SYSTEMTIME st{};
    GetSystemTime(&st);
    return system_time_to_calendar(st);
#else
    struct timespec ts {};
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return {};
    }
    struct tm utc_tm {};
    if (!gmtime_r(&ts.tv_sec, &utc_tm)) {
        return {};
    }
    return tm_to_calendar(utc_tm, as<i32>(ts.tv_nsec / 1000000L));
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
        ts.tv_nsec = as<long>(rem % 1000000000ULL);
        nanosleep(&ts, nullptr);
    }
#endif
}

} // namespace keel
