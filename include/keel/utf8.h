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

// Returns true if `b` is a UTF-8 lead byte (i.e. not a continuation byte 10xxxxxx).
[[nodiscard]] constexpr bool utf8_is_lead_byte(u8 b) noexcept { return (b & 0xC0u) != 0x80u; }

// Returns the byte index of the previous UTF-8 codepoint boundary before `index` (clamped to [0, str.size()]).
[[nodiscard]] isize utf8_prev_boundary(StringRef str, isize index) noexcept;

// Returns the byte index of the next UTF-8 codepoint boundary after `index` (clamped to [0, str.size()]).
[[nodiscard]] isize utf8_next_boundary(StringRef str, isize index) noexcept;

// Returns the byte index of the previous word boundary before `index` (for Ctrl+Left / Ctrl+Backspace).
[[nodiscard]] isize utf8_prev_word_boundary(StringRef str, isize index) noexcept;

// Returns the byte index of the next word boundary after `index` (for Ctrl+Right / Ctrl+Delete).
[[nodiscard]] isize utf8_next_word_boundary(StringRef str, isize index) noexcept;

// Computes the `[out_start, out_end)` byte range of the word or token at `index` (for double-click selection).
void utf8_word_bounds_at(StringRef str, isize index, isize& out_start, isize& out_end) noexcept;

// Unicode case folding for ASCII, Latin-1 Supplement, and Cyrillic codepoints.
[[nodiscard]] constexpr u32 utf8_to_lower(u32 cp) noexcept {
    if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
    if (cp >= 0x00C0u && cp <= 0x00DEu && cp != 0x00D7u) return cp + 0x20u; // Latin-1 Supplement
    if (cp >= 0x0410u && cp <= 0x042Fu) return cp + 0x20u;                  // Cyrillic А..Я -> а..я
    if (cp >= 0x0400u && cp <= 0x040Fu) return cp + 0x50u;                  // Cyrillic Ё, Є, І, Ї, Ў, etc.
    return cp;
}

[[nodiscard]] constexpr u32 utf8_to_upper(u32 cp) noexcept {
    if (cp >= 'a' && cp <= 'z') return cp - ('a' - 'A');
    if (cp >= 0x00E0u && cp <= 0x00FEu && cp != 0x00F7u) return cp - 0x20u; // Latin-1 Supplement
    if (cp >= 0x0430u && cp <= 0x044Fu) return cp - 0x20u;                  // Cyrillic а..я -> А..Я
    if (cp >= 0x0450u && cp <= 0x045Fu) return cp - 0x50u;                  // Cyrillic ё, є, і, ї, ў, etc.
    return cp;
}

// Finds the first UTF-8 case-insensitive occurrence of `needle` inside `haystack`.
// Returns true and populates `[out_byte_pos, out_byte_pos + out_byte_len)` when found.
[[nodiscard]] bool utf8_find_case_insensitive(
    StringRef haystack, StringRef needle, isize& out_byte_pos, isize& out_byte_len) noexcept;

// Returns the starting byte offset of the first UTF-8 case-insensitive occurrence of `needle` inside `haystack`,
// or `kInvalidIndex` (-1) if not found.
[[nodiscard]] isize utf8_find_case_insensitive(StringRef haystack, StringRef needle) noexcept;

} // namespace keel
