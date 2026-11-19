#pragma once

#include "keel/core.h"

namespace keel {

// High-resolution monotonic clock in nanoseconds (never jumps backward across NTP/wall-clock adjustments).
[[nodiscard]] u64 monotonic_ns() noexcept;

[[nodiscard]] inline u64 monotonic_us() noexcept { return monotonic_ns() / 1000ULL; }
[[nodiscard]] inline u64 monotonic_ms() noexcept { return monotonic_ns() / 1000000ULL; }
[[nodiscard]] inline f64 monotonic_sec() noexcept { return static_cast<f64>(monotonic_ns()) * 1e-9; }

// Wall-clock Unix epoch timestamp in nanoseconds (UTC).
[[nodiscard]] i64 wall_clock_ns() noexcept;

[[nodiscard]] inline i64 wall_clock_us() noexcept { return wall_clock_ns() / 1000LL; }
[[nodiscard]] inline i64 wall_clock_ms() noexcept { return wall_clock_ns() / 1000000LL; }
[[nodiscard]] inline i64 wall_clock_sec() noexcept { return wall_clock_ns() / 1000000000LL; }

// Calendar date/time broken down into Gregorian components.
struct CalendarTime {
    i32 year = 1970;     // Full Gregorian year (e.g. 2026)
    i32 month = 1;       // 1..12
    i32 day = 1;         // 1..31
    i32 hour = 0;        // 0..23
    i32 minute = 0;      // 0..59
    i32 second = 0;      // 0..59
    i32 millisecond = 0; // 0..999
    i32 day_of_week = 0; // 0 = Sunday .. 6 = Saturday
    i32 day_of_year = 1; // 1..366
};

// Thread-safe current local and UTC calendar date/time.
[[nodiscard]] CalendarTime local_time() noexcept;
[[nodiscard]] CalendarTime utc_time() noexcept;

// Suspends the calling thread for at least `ns` nanoseconds (no-op on single-threaded WebAssembly).
void sleep_ns(u64 ns) noexcept;
inline void sleep_us(u64 us) noexcept { sleep_ns(us * 1000ULL); }
inline void sleep_ms(u64 ms) noexcept { sleep_ns(ms * 1000000ULL); }

} // namespace keel
