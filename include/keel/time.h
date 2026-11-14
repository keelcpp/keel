#pragma once

#include "keel/core.h"

namespace keel {

// High-resolution monotonic clock in nanoseconds (never jumps backward across NTP/wall-clock adjustments).
[[nodiscard]] u64 monotonic_ns() noexcept;

[[nodiscard]] inline u64 monotonic_us() noexcept { return monotonic_ns() / 1000ULL; }
[[nodiscard]] inline u64 monotonic_ms() noexcept { return monotonic_ns() / 1000000ULL; }
[[nodiscard]] inline f64 monotonic_sec() noexcept { return static_cast<f64>(monotonic_ns()) * 1e-9; }

// Suspends the calling thread for at least `ns` nanoseconds (no-op on single-threaded WebAssembly).
void sleep_ns(u64 ns) noexcept;
inline void sleep_us(u64 us) noexcept { sleep_ns(us * 1000ULL); }
inline void sleep_ms(u64 ms) noexcept { sleep_ns(ms * 1000000ULL); }

} // namespace keel
