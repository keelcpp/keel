#pragma once

#include "keel/core.h"

namespace keel {

// High-quality 64-bit avalanche mixer (SplitMix64 / Murmur3 finalizer)
[[nodiscard]] constexpr u64 hash_u64(u64 x) noexcept {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

[[nodiscard]] constexpr u64 hash_combine(u64 seed, u64 val) noexcept {
    return hash_u64(seed ^ (val + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2)));
}

[[nodiscard]] u64 hash_bytes(const void* data, usize len, u64 seed = 0x9e3779b97f4a7c15ULL) noexcept;

template <typename T> struct Hash {
    [[nodiscard]] constexpr u64 operator()(const T& val) const noexcept {
        if constexpr (IsIntegral<T> || IsEnum<T>) {
            return hash_u64(static_cast<u64>(val));
        } else {
            static_assert(sizeof(T) == 0, "No keel::Hash specialization for this type");
            return 0;
        }
    }
};

template <typename T> struct Hash<T*> {
    [[nodiscard]] u64 operator()(T* ptr) const noexcept {
        return hash_u64(static_cast<u64>(reinterpret_cast<usize>(ptr)));
    }
};

template <typename T> struct Equal {
    [[nodiscard]] constexpr bool operator()(const T& a, const T& b) const noexcept { return a == b; }
};

} // namespace keel
