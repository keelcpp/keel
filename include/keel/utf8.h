#pragma once

#include "keel/ref.h"

namespace keel {

inline constexpr u32 kReplacementChar = 0xFFFDu;

// Returns true if `str` is valid UTF-8 (no overlong encodings, surrogates, or out-of-range codepoints).
[[nodiscard]] bool utf8_validate(StringRef str) noexcept;

// Decodes the next UTF-8 codepoint starting at `byte_offset` and advances `byte_offset`.
// On invalid byte sequence, advances `byte_offset` by 1 and returns `kReplacementChar` (0xFFFD).
[[nodiscard]] u32 utf8_decode_next(StringRef str, isize& byte_offset) noexcept;

// Encodes `codepoint` into `out` (at least 4 bytes) and returns the number of bytes written (1..4).
// Invalid codepoints (> 0x10FFFF or surrogates 0xD800..0xDFFF) are encoded as `kReplacementChar` (3 bytes).
isize utf8_encode(u32 codepoint, char out[4]) noexcept;

// Returns the number of Unicode codepoints in `str`.
[[nodiscard]] isize utf8_codepoint_count(StringRef str) noexcept;

} // namespace keel
