#pragma once

#include "keel/result.h"
#include "keel/string.h"

namespace keel {

// Locale-independent integer and floating-point parsing & formatting.
// Floating-point formatting produces the shortest round-trip representation.

[[nodiscard]] Result<i64, ParseError> parse_i64(StringRef str, int base = 10) noexcept;
[[nodiscard]] Result<u64, ParseError> parse_u64(StringRef str, int base = 10) noexcept;
[[nodiscard]] Result<f64, ParseError> parse_f64(StringRef str) noexcept;
[[nodiscard]] Result<f32, ParseError> parse_f32(StringRef str) noexcept;

// Writes integer representation into `buf` (up to `cap` bytes, not null-terminated) and returns length written.
usize format_i64(char* buf, usize cap, i64 value, int base = 10, bool uppercase = false) noexcept;
usize format_u64(char* buf, usize cap, u64 value, int base = 10, bool uppercase = false) noexcept;

// Shortest round-trip formatting of `value` into `buf` (up to `cap` bytes) and returns length written.
usize format_f64(char* buf, usize cap, f64 value) noexcept;
usize format_f32(char* buf, usize cap, f32 value) noexcept;

// Fixed-precision (`%.Nf`) formatting of `value` into `buf`.
usize format_f64_fixed(char* buf, usize cap, f64 value, int precision) noexcept;

// General (`%.Ng` / `{:g}`) formatting of `value` into `buf` with `precision` significant digits
// and trailing zeros stripped.
usize format_f64_general(char* buf, usize cap, f64 value, int precision = 6, bool uppercase = false) noexcept;

// Scientific (`%.Ne` / `{:e}`) formatting of `value` into `buf` with `precision` digits after the decimal point.
usize format_f64_scientific(char* buf, usize cap, f64 value, int precision = 6, bool uppercase = false) noexcept;

} // namespace keel
