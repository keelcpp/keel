#pragma once

#include "keel/core.h"

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

} // namespace keel
