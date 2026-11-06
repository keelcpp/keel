#include "keel/utf8.h"

namespace keel {

bool utf8_validate(StringRef str) noexcept {
    const u8* s = reinterpret_cast<const u8*>(str.data());
    const isize len = str.size();
    isize i = 0;
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

u32 utf8_decode_next(StringRef str, isize& byte_offset) noexcept {
    const u8* s = reinterpret_cast<const u8*>(str.data());
    const isize len = str.size();
    if (byte_offset < 0 || byte_offset >= len) {
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

isize utf8_encode(u32 cp, char out[4]) noexcept {
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

isize utf8_codepoint_count(StringRef str) noexcept {
    isize offset = 0;
    isize count = 0;
    while (offset < str.size()) {
        (void)utf8_decode_next(str, offset);
        ++count;
    }
    return count;
}

} // namespace keel
