#pragma once

#include "keel/core.h"
#include "keel/math.h"

namespace keel {

[[nodiscard]] constexpr int countl_zero(u32 x) noexcept {
    if (x == 0) return 32;
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_clz(x);
#else
    int n = 0;
    if ((x & 0xFFFF0000u) == 0) {
        n += 16;
        x <<= 16;
    }
    if ((x & 0xFF000000u) == 0) {
        n += 8;
        x <<= 8;
    }
    if ((x & 0xF0000000u) == 0) {
        n += 4;
        x <<= 4;
    }
    if ((x & 0xC0000000u) == 0) {
        n += 2;
        x <<= 2;
    }
    if ((x & 0x80000000u) == 0) {
        n += 1;
    }
    return n;
#endif
}

[[nodiscard]] constexpr int countl_zero(u64 x) noexcept {
    if (x == 0) return 64;
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_clzll(x);
#else
    const u32 hi = static_cast<u32>(x >> 32);
    return (hi != 0) ? countl_zero(hi) : (32 + countl_zero(static_cast<u32>(x)));
#endif
}

[[nodiscard]] constexpr int countr_zero(u32 x) noexcept {
    if (x == 0) return 32;
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_ctz(x);
#else
    int n = 0;
    if ((x & 0x0000FFFFu) == 0) {
        n += 16;
        x >>= 16;
    }
    if ((x & 0x000000FFu) == 0) {
        n += 8;
        x >>= 8;
    }
    if ((x & 0x0000000Fu) == 0) {
        n += 4;
        x >>= 4;
    }
    if ((x & 0x00000003u) == 0) {
        n += 2;
        x >>= 2;
    }
    if ((x & 0x00000001u) == 0) {
        n += 1;
    }
    return n;
#endif
}

[[nodiscard]] constexpr int countr_zero(u64 x) noexcept {
    if (x == 0) return 64;
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_ctzll(x);
#else
    const u32 lo = static_cast<u32>(x);
    return (lo != 0) ? countr_zero(lo) : (32 + countr_zero(static_cast<u32>(x >> 32)));
#endif
}

[[nodiscard]] constexpr int popcount(u32 x) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_popcount(x);
#else
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    return static_cast<int>((((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24);
#endif
}

[[nodiscard]] constexpr int popcount(u64 x) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_popcountll(x);
#else
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    return static_cast<int>((((x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL) * 0x0101010101010101ULL) >> 56);
#endif
}

[[nodiscard]] constexpr int bit_width(u64 x) noexcept { return 64 - countl_zero(x); }

[[nodiscard]] constexpr bool is_power_of_two(u64 x) noexcept { return x != 0 && (x & (x - 1)) == 0; }

[[nodiscard]] constexpr u64 bit_ceil(u64 x) noexcept {
    if (x <= 1) return 1;
    if (x > (u64{1} << 63)) return 0;
    return u64{1} << bit_width(x - 1);
}

[[nodiscard]] constexpr u64 bit_floor(u64 x) noexcept {
    if (x == 0) return 0;
    return u64{1} << (bit_width(x) - 1);
}

[[nodiscard]] constexpr u32 rotl(u32 x, int s) noexcept {
    const unsigned int r = static_cast<unsigned int>(s) & 31u;
    return (r == 0) ? x : ((x << r) | (x >> (32u - r)));
}

[[nodiscard]] constexpr u64 rotl(u64 x, int s) noexcept {
    const unsigned int r = static_cast<unsigned int>(s) & 63u;
    return (r == 0) ? x : ((x << r) | (x >> (64u - r)));
}

[[nodiscard]] constexpr u32 rotr(u32 x, int s) noexcept {
    const unsigned int r = static_cast<unsigned int>(s) & 31u;
    return (r == 0) ? x : ((x >> r) | (x << (32u - r)));
}

[[nodiscard]] constexpr u64 rotr(u64 x, int s) noexcept {
    const unsigned int r = static_cast<unsigned int>(s) & 63u;
    return (r == 0) ? x : ((x >> r) | (x << (64u - r)));
}

[[nodiscard]] constexpr u16 byteswap(u16 x) noexcept { return static_cast<u16>((x << 8) | (x >> 8)); }

[[nodiscard]] constexpr u32 byteswap(u32 x) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap32(x);
#else
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) | ((x & 0x00FF0000u) >> 8) |
           ((x & 0xFF000000u) >> 24);
#endif
}

[[nodiscard]] constexpr u64 byteswap(u64 x) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap64(x);
#else
    return (static_cast<u64>(byteswap(static_cast<u32>(x))) << 32) | byteswap(static_cast<u32>(x >> 32));
#endif
}

// ============================================================================
// Checked & Saturating Integer Arithmetic (constexpr, UB-free)
// ============================================================================

template <typename T>
    requires(IsIntegral<T> && !IsSame<RemoveCv<T>, bool>)
[[nodiscard]] constexpr bool checked_add(T a, T b, RemoveCv<T>& out) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return !__builtin_add_overflow(a, b, &out);
#else
    using CleanT = RemoveCv<T>;
    if constexpr (IsUnsigned<CleanT>) {
        if (static_cast<u64>(a) > static_cast<u64>(Limits<CleanT>::kMax) - static_cast<u64>(b)) {
            return false;
        }
        out = static_cast<CleanT>(static_cast<u64>(a) + static_cast<u64>(b));
        return true;
    } else {
        if (b > static_cast<CleanT>(0) && a > static_cast<CleanT>(Limits<CleanT>::kMax - b)) return false;
        if (b < static_cast<CleanT>(0) && a < static_cast<CleanT>(Limits<CleanT>::kMin - b)) return false;
        out = static_cast<CleanT>(static_cast<u64>(a) + static_cast<u64>(b));
        return true;
    }
#endif
}

template <typename T>
    requires(IsIntegral<T> && !IsSame<RemoveCv<T>, bool>)
[[nodiscard]] constexpr bool checked_sub(T a, T b, RemoveCv<T>& out) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return !__builtin_sub_overflow(a, b, &out);
#else
    using CleanT = RemoveCv<T>;
    if constexpr (IsUnsigned<CleanT>) {
        if (a < b) return false;
        out = static_cast<CleanT>(a - b);
        return true;
    } else {
        if (b < static_cast<CleanT>(0) && a > static_cast<CleanT>(Limits<CleanT>::kMax + b)) return false;
        if (b > static_cast<CleanT>(0) && a < static_cast<CleanT>(Limits<CleanT>::kMin + b)) return false;
        out = static_cast<CleanT>(static_cast<u64>(a) - static_cast<u64>(b));
        return true;
    }
#endif
}

template <typename T>
    requires(IsIntegral<T> && !IsSame<RemoveCv<T>, bool>)
[[nodiscard]] constexpr bool checked_mul(T a, T b, RemoveCv<T>& out) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return !__builtin_mul_overflow(a, b, &out);
#else
    using CleanT = RemoveCv<T>;
    if constexpr (sizeof(CleanT) < 8) {
        if constexpr (IsUnsigned<CleanT>) {
            const u64 prod = static_cast<u64>(a) * static_cast<u64>(b);
            if (prod > static_cast<u64>(Limits<CleanT>::kMax)) return false;
            out = static_cast<CleanT>(prod);
            return true;
        } else {
            const i64 prod = static_cast<i64>(a) * static_cast<i64>(b);
            if (prod < static_cast<i64>(Limits<CleanT>::kMin) || prod > static_cast<i64>(Limits<CleanT>::kMax)) {
                return false;
            }
            out = static_cast<CleanT>(prod);
            return true;
        }
    } else if constexpr (IsUnsigned<CleanT>) {
        if (a == 0 || b == 0) {
            out = 0;
            return true;
        }
        if (static_cast<u64>(a) > Limits<u64>::kMax / static_cast<u64>(b)) return false;
        out = static_cast<CleanT>(static_cast<u64>(a) * static_cast<u64>(b));
        return true;
    } else {
        if (a == 0 || b == 0) {
            out = 0;
            return true;
        }
        const bool neg = (a < 0) != (b < 0);
        const u64 ua = (a < 0) ? (0ULL - static_cast<u64>(a)) : static_cast<u64>(a);
        const u64 ub = (b < 0) ? (0ULL - static_cast<u64>(b)) : static_cast<u64>(b);
        const u64 max_mag = neg ? (static_cast<u64>(Limits<i64>::kMax) + 1ULL) : static_cast<u64>(Limits<i64>::kMax);
        if (ua > max_mag / ub) return false;
        const u64 prod = ua * ub;
        out = neg ? static_cast<CleanT>(0ULL - prod) : static_cast<CleanT>(prod);
        return true;
    }
#endif
}

template <typename T>
    requires(IsIntegral<T> && !IsSame<RemoveCv<T>, bool>)
[[nodiscard]] constexpr RemoveCv<T> sat_add(T a, T b) noexcept {
    using CleanT = RemoveCv<T>;
    CleanT res{};
    if (checked_add(a, b, res)) return res;
    if constexpr (IsUnsigned<CleanT>) {
        return Limits<CleanT>::kMax;
    } else {
        return (b < static_cast<CleanT>(0)) ? Limits<CleanT>::kMin : Limits<CleanT>::kMax;
    }
}

template <typename T>
    requires(IsIntegral<T> && !IsSame<RemoveCv<T>, bool>)
[[nodiscard]] constexpr RemoveCv<T> sat_sub(T a, T b) noexcept {
    using CleanT = RemoveCv<T>;
    CleanT res{};
    if (checked_sub(a, b, res)) return res;
    if constexpr (IsUnsigned<CleanT>) {
        return Limits<CleanT>::kMin;
    } else {
        return (b < static_cast<CleanT>(0)) ? Limits<CleanT>::kMax : Limits<CleanT>::kMin;
    }
}

template <typename T>
    requires(IsIntegral<T> && !IsSame<RemoveCv<T>, bool>)
[[nodiscard]] constexpr RemoveCv<T> sat_mul(T a, T b) noexcept {
    using CleanT = RemoveCv<T>;
    CleanT res{};
    if (checked_mul(a, b, res)) return res;
    if constexpr (IsUnsigned<CleanT>) {
        return Limits<CleanT>::kMax;
    } else {
        const bool neg = (a < static_cast<CleanT>(0)) != (b < static_cast<CleanT>(0));
        return neg ? Limits<CleanT>::kMin : Limits<CleanT>::kMax;
    }
}

template <typename To, typename From>
    requires(IsIntegral<To> && !IsSame<RemoveCv<To>, bool> && IsIntegral<From> && !IsSame<RemoveCv<From>, bool>)
[[nodiscard]] constexpr RemoveCv<To> sat_cast(From x) noexcept {
    using CleanTo = RemoveCv<To>;
    using CleanFrom = RemoveCv<From>;
    if constexpr (IsSigned<CleanFrom> && IsUnsigned<CleanTo>) {
        if (x <= static_cast<CleanFrom>(0)) return static_cast<CleanTo>(0);
        const u64 ux = static_cast<u64>(x);
        return (ux > static_cast<u64>(Limits<CleanTo>::kMax)) ? Limits<CleanTo>::kMax : static_cast<CleanTo>(ux);
    } else if constexpr (IsUnsigned<CleanFrom>) {
        const u64 ux = static_cast<u64>(x);
        return (ux > static_cast<u64>(Limits<CleanTo>::kMax)) ? Limits<CleanTo>::kMax : static_cast<CleanTo>(ux);
    } else {
        const i64 sx = static_cast<i64>(x);
        if (sx < static_cast<i64>(Limits<CleanTo>::kMin)) return Limits<CleanTo>::kMin;
        if (sx > static_cast<i64>(Limits<CleanTo>::kMax)) return Limits<CleanTo>::kMax;
        return static_cast<CleanTo>(sx);
    }
}

// ============================================================================
// Unaligned Endian Load / Store (constexpr, aliasing-safe)
// ============================================================================

[[nodiscard]] constexpr u16 read_le16(const u8* p) noexcept {
    return static_cast<u16>(static_cast<u16>(p[0]) | (static_cast<u16>(p[1]) << 8));
}

[[nodiscard]] constexpr u32 read_le32(const u8* p) noexcept {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

[[nodiscard]] constexpr u64 read_le64(const u8* p) noexcept {
    return static_cast<u64>(read_le32(p)) | (static_cast<u64>(read_le32(p + 4)) << 32);
}

[[nodiscard]] constexpr u16 read_be16(const u8* p) noexcept {
    return static_cast<u16>((static_cast<u16>(p[0]) << 8) | static_cast<u16>(p[1]));
}

[[nodiscard]] constexpr u32 read_be32(const u8* p) noexcept {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8) |
           static_cast<u32>(p[3]);
}

[[nodiscard]] constexpr u64 read_be64(const u8* p) noexcept {
    return (static_cast<u64>(read_be32(p)) << 32) | static_cast<u64>(read_be32(p + 4));
}

constexpr void write_le16(u8* p, u16 v) noexcept {
    p[0] = static_cast<u8>(v & 0xFFu);
    p[1] = static_cast<u8>((v >> 8) & 0xFFu);
}

constexpr void write_le32(u8* p, u32 v) noexcept {
    p[0] = static_cast<u8>(v & 0xFFu);
    p[1] = static_cast<u8>((v >> 8) & 0xFFu);
    p[2] = static_cast<u8>((v >> 16) & 0xFFu);
    p[3] = static_cast<u8>((v >> 24) & 0xFFu);
}

constexpr void write_le64(u8* p, u64 v) noexcept {
    write_le32(p, static_cast<u32>(v));
    write_le32(p + 4, static_cast<u32>(v >> 32));
}

constexpr void write_be16(u8* p, u16 v) noexcept {
    p[0] = static_cast<u8>((v >> 8) & 0xFFu);
    p[1] = static_cast<u8>(v & 0xFFu);
}

constexpr void write_be32(u8* p, u32 v) noexcept {
    p[0] = static_cast<u8>((v >> 24) & 0xFFu);
    p[1] = static_cast<u8>((v >> 16) & 0xFFu);
    p[2] = static_cast<u8>((v >> 8) & 0xFFu);
    p[3] = static_cast<u8>(v & 0xFFu);
}

constexpr void write_be64(u8* p, u64 v) noexcept {
    write_be32(p, static_cast<u32>(v >> 32));
    write_be32(p + 4, static_cast<u32>(v));
}

} // namespace keel
