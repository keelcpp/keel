#pragma once

#include "keel/core.h"

namespace keel {

// High-resolution monotonic clock in nanoseconds (never jumps backward across NTP/wall-clock adjustments).
[[nodiscard]] u64 monotonic_ns() noexcept;

[[nodiscard]] inline u64 monotonic_us() noexcept { return monotonic_ns() / 1000ULL; }
[[nodiscard]] inline u64 monotonic_ms() noexcept { return monotonic_ns() / 1000000ULL; }
[[nodiscard]] inline f64 monotonic_sec() noexcept { return static_cast<f64>(monotonic_ns()) * 1e-9; }

} // namespace keel
