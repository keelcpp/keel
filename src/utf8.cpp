#include "keel/utf8.h"

namespace keel {

bool utf8_validate(StringRef str) noexcept {
    const u8* s = reinterpret_cast<const u8*>(str.data());
    const usize len = str.size();
    usize i = 0;
    while (i < len) {
        const u8 b0 = s[i];
        if (b0 < 0x80u) {
            ++i;
            continue;
        }
        if (b0 < 0xC2u) {
            return false; // Continuation byte or overlong 2-byte sequence (0xC0, 0xC1)
        }
        if (b0 < 0xE0u) {
            if (i + 1 >= len) return false;
            const u8 b1 = s[i + 1];
            if ((b1 & 0xC0u) != 0x80u) return false;
            i += 2;
            continue;
        }
        if (b0 < 0xF0u) {
            if (i + 2 >= len) return false;
            const u8 b1 = s[i + 1];
            const u8 b2 = s[i + 2];
            if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) return false;
            if (b0 == 0xE0u && b1 < 0xA0u) return false; // Overlong 3-byte
            if (b0 == 0xEDu && b1 >= 0xA0u) return false; // UTF-16 surrogate halves (U+D800..U+DFFF)
            i += 3;
            continue;
        }
        if (b0 <= 0xF4u) {
            if (i + 3 >= len) return false;
            const u8 b1 = s[i + 1];
            const u8 b2 = s[i + 2];
            const u8 b3 = s[i + 3];
            if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) return false;
            if (b0 == 0xF0u && b1 < 0x90u) return false; // Overlong 4-byte
            if (b0 == 0xF4u && b1 >= 0x90u) return false; // > U+10FFFF
            i += 4;
            continue;
        }
        return false;
    }
    return true;
}

u32 utf8_decode_next(StringRef str, usize& byte_offset) noexcept {
    const u8* s = reinterpret_cast<const u8*>(str.data());
    const usize len = str.size();
    if (byte_offset >= len) {
        return 0;
    }

    const u8 b0 = s[byte_offset];
    if (b0 < 0x80u) {
        ++byte_offset;
        return b0;
    }
    if (b0 >= 0xC2u && b0 < 0xE0u) {
        if (byte_offset + 1 < len) {
            const u8 b1 = s[byte_offset + 1];
            if ((b1 & 0xC0u) == 0x80u) {
                byte_offset += 2;
                return (static_cast<u32>(b0 & 0x1Fu) << 6) | static_cast<u32>(b1 & 0x3Fu);
            }
        }
        ++byte_offset;
        return kReplacementChar;
    }
    if (b0 >= 0xE0u && b0 < 0xF0u) {
        if (byte_offset + 2 < len) {
            const u8 b1 = s[byte_offset + 1];
            const u8 b2 = s[byte_offset + 2];
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && !(b0 == 0xE0u && b1 < 0xA0u) &&
                !(b0 == 0xEDu && b1 >= 0xA0u)) {
                byte_offset += 3;
                return (static_cast<u32>(b0 & 0x0Fu) << 12) | (static_cast<u32>(b1 & 0x3Fu) << 6) |
                       static_cast<u32>(b2 & 0x3Fu);
            }
        }
        ++byte_offset;
        return kReplacementChar;
    }
    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        if (byte_offset + 3 < len) {
            const u8 b1 = s[byte_offset + 1];
            const u8 b2 = s[byte_offset + 2];
            const u8 b3 = s[byte_offset + 3];
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u &&
                !(b0 == 0xF0u && b1 < 0x90u) && !(b0 == 0xF4u && b1 >= 0x90u)) {
                byte_offset += 4;
                return (static_cast<u32>(b0 & 0x07u) << 18) | (static_cast<u32>(b1 & 0x3Fu) << 12) |
                       (static_cast<u32>(b2 & 0x3Fu) << 6) | static_cast<u32>(b3 & 0x3Fu);
            }
        }
        ++byte_offset;
        return kReplacementChar;
    }

    ++byte_offset;
    return kReplacementChar;
}

usize utf8_encode(u32 cp, char out[4]) noexcept {
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        cp = kReplacementChar;
    }
    if (cp < 0x80u) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = static_cast<char>(0xC0u | (cp >> 6));
        out[1] = static_cast<char>(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = static_cast<char>(0xE0u | (cp >> 12));
        out[1] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = static_cast<char>(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = static_cast<char>(0xF0u | (cp >> 18));
    out[1] = static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = static_cast<char>(0x80u | (cp & 0x3Fu));
    return 4;
}

usize utf8_codepoint_count(StringRef str) noexcept {
    usize offset = 0;
    usize count = 0;
    while (offset < str.size()) {
        (void)utf8_decode_next(str, offset);
        ++count;
    }
    return count;
}

namespace {

enum class Utf8CharCategory : u8 {
    Whitespace = 0,
    Word,
    Punctuation
};

[[nodiscard]] Utf8CharCategory classify_utf8_char(StringRef text, usize char_start) noexcept {
    if (char_start >= text.size()) {
        return Utf8CharCategory::Whitespace;
    }
    const u8 b = static_cast<u8>(text[char_start]);
    if (b == ' ' || b == '\t' || b == '\n' || b == '\r') {
        return Utf8CharCategory::Whitespace;
    }
    if (b >= 0x80u || (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_') {
        return Utf8CharCategory::Word;
    }
    return Utf8CharCategory::Punctuation;
}

} // namespace

usize utf8_prev_boundary(StringRef str, usize index) noexcept {
    if (str.empty() || index == 0) {
        return 0;
    }
    if (index > str.size()) {
        index = str.size();
    }
    --index;
    while (index > 0 && !utf8_is_lead_byte(static_cast<u8>(str[index]))) {
        --index;
    }
    return index;
}

usize utf8_next_boundary(StringRef str, usize index) noexcept {
    const usize n = str.size();
    if (n == 0 || index >= n) {
        return n;
    }
    ++index;
    while (index < n && !utf8_is_lead_byte(static_cast<u8>(str[index]))) {
        ++index;
    }
    return index;
}

usize utf8_prev_word_boundary(StringRef str, usize index) noexcept {
    if (str.empty() || index == 0) {
        return 0;
    }
    usize pos = keel::min(index, str.size());

    while (pos > 0) {
        const usize prev = utf8_prev_boundary(str, pos);
        if (classify_utf8_char(str, prev) != Utf8CharCategory::Whitespace) {
            break;
        }
        pos = prev;
    }
    if (pos == 0) {
        return 0;
    }

    const usize first_char = utf8_prev_boundary(str, pos);
    const Utf8CharCategory target_cat = classify_utf8_char(str, first_char);
    pos = first_char;
    while (pos > 0) {
        const usize prev = utf8_prev_boundary(str, pos);
        if (classify_utf8_char(str, prev) != target_cat) {
            break;
        }
        pos = prev;
    }
    return pos;
}

usize utf8_next_word_boundary(StringRef str, usize index) noexcept {
    const usize n = str.size();
    if (n == 0 || index >= n) {
        return n;
    }
    usize pos = index;
    while (pos > 0 && !utf8_is_lead_byte(static_cast<u8>(str[pos]))) {
        --pos;
    }

    while (pos < n && classify_utf8_char(str, pos) == Utf8CharCategory::Whitespace) {
        pos = utf8_next_boundary(str, pos);
    }
    if (pos >= n) {
        return n;
    }

    const Utf8CharCategory target_cat = classify_utf8_char(str, pos);
    while (pos < n && classify_utf8_char(str, pos) == target_cat) {
        pos = utf8_next_boundary(str, pos);
    }
    return pos;
}

void utf8_word_bounds_at(StringRef str, usize index, usize& out_start, usize& out_end) noexcept {
    if (str.empty()) {
        out_start = 0;
        out_end = 0;
        return;
    }
    const usize n = str.size();
    usize pos = keel::min(index, n);
    if (pos == n) {
        pos = utf8_prev_boundary(str, n);
    } else {
        while (pos > 0 && !utf8_is_lead_byte(static_cast<u8>(str[pos]))) {
            --pos;
        }
    }

    const Utf8CharCategory cat = classify_utf8_char(str, pos);
    usize start = pos;
    while (start > 0) {
        const usize prev = utf8_prev_boundary(str, start);
        if (classify_utf8_char(str, prev) != cat) {
            break;
        }
        start = prev;
    }

    usize end = utf8_next_boundary(str, pos);
    while (end < n && classify_utf8_char(str, end) == cat) {
        end = utf8_next_boundary(str, end);
    }

    out_start = start;
    out_end = end;
}

bool utf8_find_case_insensitive(
    StringRef haystack, StringRef needle, usize& out_byte_pos, usize& out_byte_len) noexcept {
    out_byte_pos = 0;
    out_byte_len = 0;
    if (needle.empty()) {
        return true;
    }
    if (haystack.empty()) {
        return false;
    }

    usize h_start = 0;
    while (h_start < haystack.size()) {
        const usize match_start = h_start;
        usize h_cursor = h_start;
        usize n_cursor = 0;
        bool matched = true;
        while (n_cursor < needle.size()) {
            if (h_cursor >= haystack.size()) {
                matched = false;
                break;
            }
            const u32 h_cp = utf8_to_lower(utf8_decode_next(haystack, h_cursor));
            const u32 n_cp = utf8_to_lower(utf8_decode_next(needle, n_cursor));
            if (h_cp != n_cp) {
                matched = false;
                break;
            }
        }
        if (matched) {
            out_byte_pos = match_start;
            out_byte_len = h_cursor - match_start;
            return true;
        }
        (void)utf8_decode_next(haystack, h_start);
    }
    return false;
}

usize utf8_find_case_insensitive(StringRef haystack, StringRef needle) noexcept {
    usize pos = 0;
    usize len = 0;
    return utf8_find_case_insensitive(haystack, needle, pos, len) ? pos : kInvalidIndex;
}

} // namespace keel
