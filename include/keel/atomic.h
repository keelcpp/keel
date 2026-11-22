#pragma once

#include "keel/core.h"

namespace keel {

enum class MemoryOrder : i32 {
    Relaxed = 0,
    Acquire = 2,
    Release = 3,
    AcqRel = 4,
    SeqCst = 5
};

namespace detail {

template <usize Bytes> struct AtomicStorageType;
template <> struct AtomicStorageType<1> { using Type = u8; };
template <> struct AtomicStorageType<2> { using Type = u16; };
template <> struct AtomicStorageType<4> { using Type = u32; };
template <> struct AtomicStorageType<8> { using Type = u64; };

template <typename T>
concept AtomicValueType = (IsIntegral<T> || IsEnum<T> || IsPointer<T>) && !IsConst<T> &&
                          (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

template <typename T>
[[nodiscard]] inline typename AtomicStorageType<sizeof(T)>::Type atomic_to_bits(T val) noexcept {
    using U = typename AtomicStorageType<sizeof(T)>::Type;
    if constexpr (IsPointer<T>) {
        return static_cast<U>(reinterpret_cast<usize>(val));
    } else if constexpr (IsEnum<T>) {
        return static_cast<U>(static_cast<UnderlyingType<T>>(val));
    } else {
        return static_cast<U>(val);
    }
}

template <typename T>
[[nodiscard]] inline T atomic_from_bits(typename AtomicStorageType<sizeof(T)>::Type bits) noexcept {
    if constexpr (IsPointer<T>) {
        return reinterpret_cast<T>(static_cast<usize>(bits));
    } else if constexpr (IsEnum<T>) {
        return static_cast<T>(static_cast<UnderlyingType<T>>(bits));
    } else if constexpr (IsSame<T, bool>) {
        return bits != 0;
    } else {
        return static_cast<T>(bits);
    }
}

#if defined(__clang__) || defined(__GNUC__)

[[nodiscard]] constexpr int to_builtin_order(MemoryOrder order) noexcept {
    switch (order) {
    case MemoryOrder::Relaxed:
        return __ATOMIC_RELAXED;
    case MemoryOrder::Acquire:
        return __ATOMIC_ACQUIRE;
    case MemoryOrder::Release:
        return __ATOMIC_RELEASE;
    case MemoryOrder::AcqRel:
        return __ATOMIC_ACQ_REL;
    case MemoryOrder::SeqCst:
    default:
        return __ATOMIC_SEQ_CST;
    }
}

[[nodiscard]] constexpr int to_builtin_load_order(MemoryOrder order) noexcept {
    return (order == MemoryOrder::Release || order == MemoryOrder::AcqRel) ? __ATOMIC_ACQUIRE
                                                                           : to_builtin_order(order);
}

[[nodiscard]] constexpr int to_builtin_store_order(MemoryOrder order) noexcept {
    return (order == MemoryOrder::Acquire || order == MemoryOrder::AcqRel) ? __ATOMIC_RELEASE
                                                                           : to_builtin_order(order);
}

template <typename U>
[[nodiscard]] inline U atomic_raw_load(const U* ptr, MemoryOrder order) noexcept {
    return __atomic_load_n(ptr, to_builtin_load_order(order));
}

template <typename U>
inline void atomic_raw_store(U* ptr, U val, MemoryOrder order) noexcept {
    __atomic_store_n(ptr, val, to_builtin_store_order(order));
}

template <typename U>
[[nodiscard]] inline U atomic_raw_exchange(U* ptr, U val, MemoryOrder order) noexcept {
    return __atomic_exchange_n(ptr, val, to_builtin_order(order));
}

template <typename U>
[[nodiscard]] inline bool atomic_raw_cas(U* ptr, U* expected, U desired, bool weak,
                                         MemoryOrder success, MemoryOrder failure) noexcept {
    return __atomic_compare_exchange_n(ptr, expected, desired, weak,
                                       to_builtin_order(success), to_builtin_load_order(failure));
}

template <typename U>
[[nodiscard]] inline U atomic_raw_fetch_add(U* ptr, U delta, MemoryOrder order) noexcept {
    return __atomic_fetch_add(ptr, delta, to_builtin_order(order));
}

template <typename U>
[[nodiscard]] inline U atomic_raw_fetch_sub(U* ptr, U delta, MemoryOrder order) noexcept {
    return __atomic_fetch_sub(ptr, delta, to_builtin_order(order));
}

template <typename U>
[[nodiscard]] inline U atomic_raw_fetch_and(U* ptr, U mask, MemoryOrder order) noexcept {
    return __atomic_fetch_and(ptr, mask, to_builtin_order(order));
}

template <typename U>
[[nodiscard]] inline U atomic_raw_fetch_or(U* ptr, U mask, MemoryOrder order) noexcept {
    return __atomic_fetch_or(ptr, mask, to_builtin_order(order));
}

template <typename U>
[[nodiscard]] inline U atomic_raw_fetch_xor(U* ptr, U mask, MemoryOrder order) noexcept {
    return __atomic_fetch_xor(ptr, mask, to_builtin_order(order));
}

inline void atomic_raw_fence(MemoryOrder order) noexcept {
    __atomic_thread_fence(to_builtin_order(order));
}

#else // MSVC (out-of-line in src/atomic.cpp so <intrin.h> never leaks into public headers)

u8 atomic_raw_load(const u8* ptr, MemoryOrder order) noexcept;
u16 atomic_raw_load(const u16* ptr, MemoryOrder order) noexcept;
u32 atomic_raw_load(const u32* ptr, MemoryOrder order) noexcept;
u64 atomic_raw_load(const u64* ptr, MemoryOrder order) noexcept;

void atomic_raw_store(u8* ptr, u8 val, MemoryOrder order) noexcept;
void atomic_raw_store(u16* ptr, u16 val, MemoryOrder order) noexcept;
void atomic_raw_store(u32* ptr, u32 val, MemoryOrder order) noexcept;
void atomic_raw_store(u64* ptr, u64 val, MemoryOrder order) noexcept;

u8 atomic_raw_exchange(u8* ptr, u8 val, MemoryOrder order) noexcept;
u16 atomic_raw_exchange(u16* ptr, u16 val, MemoryOrder order) noexcept;
u32 atomic_raw_exchange(u32* ptr, u32 val, MemoryOrder order) noexcept;
u64 atomic_raw_exchange(u64* ptr, u64 val, MemoryOrder order) noexcept;

bool atomic_raw_cas(u8* ptr, u8* expected, u8 desired, bool weak, MemoryOrder success, MemoryOrder failure) noexcept;
bool atomic_raw_cas(u16* ptr, u16* expected, u16 desired, bool weak, MemoryOrder success, MemoryOrder failure) noexcept;
bool atomic_raw_cas(u32* ptr, u32* expected, u32 desired, bool weak, MemoryOrder success, MemoryOrder failure) noexcept;
bool atomic_raw_cas(u64* ptr, u64* expected, u64 desired, bool weak, MemoryOrder success, MemoryOrder failure) noexcept;

u8 atomic_raw_fetch_add(u8* ptr, u8 delta, MemoryOrder order) noexcept;
u16 atomic_raw_fetch_add(u16* ptr, u16 delta, MemoryOrder order) noexcept;
u32 atomic_raw_fetch_add(u32* ptr, u32 delta, MemoryOrder order) noexcept;
u64 atomic_raw_fetch_add(u64* ptr, u64 delta, MemoryOrder order) noexcept;

u8 atomic_raw_fetch_sub(u8* ptr, u8 delta, MemoryOrder order) noexcept;
u16 atomic_raw_fetch_sub(u16* ptr, u16 delta, MemoryOrder order) noexcept;
u32 atomic_raw_fetch_sub(u32* ptr, u32 delta, MemoryOrder order) noexcept;
u64 atomic_raw_fetch_sub(u64* ptr, u64 delta, MemoryOrder order) noexcept;

u8 atomic_raw_fetch_and(u8* ptr, u8 mask, MemoryOrder order) noexcept;
u16 atomic_raw_fetch_and(u16* ptr, u16 mask, MemoryOrder order) noexcept;
u32 atomic_raw_fetch_and(u32* ptr, u32 mask, MemoryOrder order) noexcept;
u64 atomic_raw_fetch_and(u64* ptr, u64 mask, MemoryOrder order) noexcept;

u8 atomic_raw_fetch_or(u8* ptr, u8 mask, MemoryOrder order) noexcept;
u16 atomic_raw_fetch_or(u16* ptr, u16 mask, MemoryOrder order) noexcept;
u32 atomic_raw_fetch_or(u32* ptr, u32 mask, MemoryOrder order) noexcept;
u64 atomic_raw_fetch_or(u64* ptr, u64 mask, MemoryOrder order) noexcept;

u8 atomic_raw_fetch_xor(u8* ptr, u8 mask, MemoryOrder order) noexcept;
u16 atomic_raw_fetch_xor(u16* ptr, u16 mask, MemoryOrder order) noexcept;
u32 atomic_raw_fetch_xor(u32* ptr, u32 mask, MemoryOrder order) noexcept;
u64 atomic_raw_fetch_xor(u64* ptr, u64 mask, MemoryOrder order) noexcept;

void atomic_raw_fence(MemoryOrder order) noexcept;

#endif

} // namespace detail

inline void atomic_thread_fence(MemoryOrder order = MemoryOrder::SeqCst) noexcept {
    detail::atomic_raw_fence(order);
}

// Zero-STL lock-free atomic wrapper for integral, enum, and pointer types (1, 2, 4, or 8 bytes).
template <detail::AtomicValueType T>
class Atomic {
public:
    using ValueType = T;
    static constexpr bool is_always_lock_free = true;

    constexpr Atomic() noexcept = default;

    constexpr Atomic(T desired) noexcept
        requires(!IsPointer<T>)
        : m_bits(init_nonptr_bits(desired)) {}

    constexpr Atomic(decltype(nullptr)) noexcept
        requires(IsPointer<T>)
        : m_bits(0) {}

    Atomic(T desired) noexcept
        requires(IsPointer<T>)
        : m_bits(static_cast<StorageU>(reinterpret_cast<usize>(desired))) {}

    Atomic(const Atomic&) = delete;
    Atomic& operator=(const Atomic&) = delete;

    [[nodiscard]] T load(MemoryOrder order = MemoryOrder::SeqCst) const noexcept {
        return detail::atomic_from_bits<T>(detail::atomic_raw_load(&m_bits, order));
    }

    void store(T desired, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        detail::atomic_raw_store(&m_bits, detail::atomic_to_bits<T>(desired), order);
    }

    T exchange(T desired, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return detail::atomic_from_bits<T>(
            detail::atomic_raw_exchange(&m_bits, detail::atomic_to_bits<T>(desired), order));
    }

    [[nodiscard]] bool compare_exchange_weak(T& expected, T desired,
                                             MemoryOrder success = MemoryOrder::SeqCst,
                                             MemoryOrder failure = MemoryOrder::SeqCst) noexcept {
        StorageU exp_bits = detail::atomic_to_bits<T>(expected);
        const bool ok = detail::atomic_raw_cas(&m_bits, &exp_bits, detail::atomic_to_bits<T>(desired),
                                               true, success, failure);
        if (!ok) expected = detail::atomic_from_bits<T>(exp_bits);
        return ok;
    }

    [[nodiscard]] bool compare_exchange_strong(T& expected, T desired,
                                               MemoryOrder success = MemoryOrder::SeqCst,
                                               MemoryOrder failure = MemoryOrder::SeqCst) noexcept {
        StorageU exp_bits = detail::atomic_to_bits<T>(expected);
        const bool ok = detail::atomic_raw_cas(&m_bits, &exp_bits, detail::atomic_to_bits<T>(desired),
                                               false, success, failure);
        if (!ok) expected = detail::atomic_from_bits<T>(exp_bits);
        return ok;
    }

    T fetch_add(T delta, MemoryOrder order = MemoryOrder::SeqCst) noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return detail::atomic_from_bits<T>(
            detail::atomic_raw_fetch_add(&m_bits, detail::atomic_to_bits<T>(delta), order));
    }

    T fetch_sub(T delta, MemoryOrder order = MemoryOrder::SeqCst) noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return detail::atomic_from_bits<T>(
            detail::atomic_raw_fetch_sub(&m_bits, detail::atomic_to_bits<T>(delta), order));
    }

    T fetch_add(isize delta, MemoryOrder order = MemoryOrder::SeqCst) noexcept
        requires(IsPointer<T> && !IsSame<RemovePointer<T>, void>)
    {
        const StorageU byte_delta =
            static_cast<StorageU>(delta * static_cast<isize>(sizeof(RemovePointer<T>)));
        return detail::atomic_from_bits<T>(detail::atomic_raw_fetch_add(&m_bits, byte_delta, order));
    }

    T fetch_sub(isize delta, MemoryOrder order = MemoryOrder::SeqCst) noexcept
        requires(IsPointer<T> && !IsSame<RemovePointer<T>, void>)
    {
        const StorageU byte_delta =
            static_cast<StorageU>(delta * static_cast<isize>(sizeof(RemovePointer<T>)));
        return detail::atomic_from_bits<T>(detail::atomic_raw_fetch_sub(&m_bits, byte_delta, order));
    }

    T fetch_and(T mask, MemoryOrder order = MemoryOrder::SeqCst) noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return detail::atomic_from_bits<T>(
            detail::atomic_raw_fetch_and(&m_bits, detail::atomic_to_bits<T>(mask), order));
    }

    T fetch_or(T mask, MemoryOrder order = MemoryOrder::SeqCst) noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return detail::atomic_from_bits<T>(
            detail::atomic_raw_fetch_or(&m_bits, detail::atomic_to_bits<T>(mask), order));
    }

    T fetch_xor(T mask, MemoryOrder order = MemoryOrder::SeqCst) noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return detail::atomic_from_bits<T>(
            detail::atomic_raw_fetch_xor(&m_bits, detail::atomic_to_bits<T>(mask), order));
    }

    T operator=(T desired) noexcept {
        store(desired);
        return desired;
    }

    [[nodiscard]] operator T() const noexcept { return load(); }

    T operator++() noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return static_cast<T>(fetch_add(static_cast<T>(1)) + static_cast<T>(1));
    }

    T operator++(int) noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return fetch_add(static_cast<T>(1));
    }

    T operator--() noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return static_cast<T>(fetch_sub(static_cast<T>(1)) - static_cast<T>(1));
    }

    T operator--(int) noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return fetch_sub(static_cast<T>(1));
    }

    T operator+=(T delta) noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return static_cast<T>(fetch_add(delta) + delta);
    }

    T operator-=(T delta) noexcept
        requires(IsIntegral<T> && !IsSame<T, bool>)
    {
        return static_cast<T>(fetch_sub(delta) - delta);
    }

private:
    using StorageU = typename detail::AtomicStorageType<sizeof(T)>::Type;

    static constexpr StorageU init_nonptr_bits(T val) noexcept
        requires(!IsPointer<T>)
    {
        if constexpr (IsEnum<T>) {
            return static_cast<StorageU>(static_cast<UnderlyingType<T>>(val));
        } else {
            return static_cast<StorageU>(val);
        }
    }

    alignas(sizeof(T)) StorageU m_bits{0};
};

} // namespace keel
