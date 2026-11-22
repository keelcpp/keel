#include "keel/atomic.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>

namespace keel::detail {

namespace {

inline void msvc_hw_barrier_acq_rel() noexcept {
#if defined(_M_ARM64)
    __dmb(_ARM64_BARRIER_ISH);
#elif defined(_M_ARM)
    __dmb(_ARM_BARRIER_ISH);
#else
    // x86/x64 TSO enforces load-acquire and store-release in hardware; compiler barrier suffices.
    _ReadWriteBarrier();
#endif
}

inline void msvc_hw_barrier_seq_cst() noexcept {
#if defined(_M_ARM64)
    __dmb(_ARM64_BARRIER_ISH);
#elif defined(_M_ARM)
    __dmb(_ARM_BARRIER_ISH);
#else
    volatile long dummy = 0;
    (void)_InterlockedOr(&dummy, 0);
#endif
}

} // namespace

u8 atomic_raw_load(const u8* ptr, MemoryOrder order) noexcept {
    if (order == MemoryOrder::SeqCst) {
        return static_cast<u8>(_InterlockedOr8(reinterpret_cast<volatile char*>(const_cast<u8*>(ptr)), 0));
    }
    const u8 v = static_cast<u8>(__iso_volatile_load8(reinterpret_cast<const volatile __int8*>(ptr)));
    if (order != MemoryOrder::Relaxed) msvc_hw_barrier_acq_rel();
    return v;
}

u16 atomic_raw_load(const u16* ptr, MemoryOrder order) noexcept {
    if (order == MemoryOrder::SeqCst) {
        return static_cast<u16>(_InterlockedOr16(reinterpret_cast<volatile short*>(const_cast<u16*>(ptr)), 0));
    }
    const u16 v = static_cast<u16>(__iso_volatile_load16(reinterpret_cast<const volatile __int16*>(ptr)));
    if (order != MemoryOrder::Relaxed) msvc_hw_barrier_acq_rel();
    return v;
}

u32 atomic_raw_load(const u32* ptr, MemoryOrder order) noexcept {
    if (order == MemoryOrder::SeqCst) {
        return static_cast<u32>(_InterlockedOr(reinterpret_cast<volatile long*>(const_cast<u32*>(ptr)), 0));
    }
    const u32 v = static_cast<u32>(__iso_volatile_load32(reinterpret_cast<const volatile __int32*>(ptr)));
    if (order != MemoryOrder::Relaxed) msvc_hw_barrier_acq_rel();
    return v;
}

u64 atomic_raw_load(const u64* ptr, MemoryOrder order) noexcept {
#if defined(_M_X64) || defined(_M_ARM64)
    if (order == MemoryOrder::SeqCst) {
        return static_cast<u64>(
            _InterlockedCompareExchange64(reinterpret_cast<volatile __int64*>(const_cast<u64*>(ptr)), 0, 0));
    }
    const u64 v = static_cast<u64>(__iso_volatile_load64(reinterpret_cast<const volatile __int64*>(ptr)));
    if (order != MemoryOrder::Relaxed) msvc_hw_barrier_acq_rel();
    return v;
#else
    (void)order;
    return static_cast<u64>(
        _InterlockedCompareExchange64(reinterpret_cast<volatile __int64*>(const_cast<u64*>(ptr)), 0, 0));
#endif
}

void atomic_raw_store(u8* ptr, u8 val, MemoryOrder order) noexcept {
    if (order == MemoryOrder::SeqCst) {
        (void)_InterlockedExchange8(reinterpret_cast<volatile char*>(ptr), static_cast<char>(val));
    } else {
        if (order != MemoryOrder::Relaxed) msvc_hw_barrier_acq_rel();
        __iso_volatile_store8(reinterpret_cast<volatile __int8*>(ptr), static_cast<__int8>(val));
    }
}

void atomic_raw_store(u16* ptr, u16 val, MemoryOrder order) noexcept {
    if (order == MemoryOrder::SeqCst) {
        (void)_InterlockedExchange16(reinterpret_cast<volatile short*>(ptr), static_cast<short>(val));
    } else {
        if (order != MemoryOrder::Relaxed) msvc_hw_barrier_acq_rel();
        __iso_volatile_store16(reinterpret_cast<volatile __int16*>(ptr), static_cast<__int16>(val));
    }
}

void atomic_raw_store(u32* ptr, u32 val, MemoryOrder order) noexcept {
    if (order == MemoryOrder::SeqCst) {
        (void)_InterlockedExchange(reinterpret_cast<volatile long*>(ptr), static_cast<long>(val));
    } else {
        if (order != MemoryOrder::Relaxed) msvc_hw_barrier_acq_rel();
        __iso_volatile_store32(reinterpret_cast<volatile __int32*>(ptr), static_cast<__int32>(val));
    }
}

void atomic_raw_store(u64* ptr, u64 val, MemoryOrder order) noexcept {
#if defined(_M_X64) || defined(_M_ARM64)
    if (order == MemoryOrder::SeqCst) {
        (void)_InterlockedExchange64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(val));
    } else {
        if (order != MemoryOrder::Relaxed) msvc_hw_barrier_acq_rel();
        __iso_volatile_store64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(val));
    }
#else
    (void)order;
    (void)_InterlockedExchange64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(val));
#endif
}

u8 atomic_raw_exchange(u8* ptr, u8 val, MemoryOrder) noexcept {
    return static_cast<u8>(_InterlockedExchange8(reinterpret_cast<volatile char*>(ptr), static_cast<char>(val)));
}

u16 atomic_raw_exchange(u16* ptr, u16 val, MemoryOrder) noexcept {
    return static_cast<u16>(_InterlockedExchange16(reinterpret_cast<volatile short*>(ptr), static_cast<short>(val)));
}

u32 atomic_raw_exchange(u32* ptr, u32 val, MemoryOrder) noexcept {
    return static_cast<u32>(_InterlockedExchange(reinterpret_cast<volatile long*>(ptr), static_cast<long>(val)));
}

u64 atomic_raw_exchange(u64* ptr, u64 val, MemoryOrder) noexcept {
    return static_cast<u64>(_InterlockedExchange64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(val)));
}

bool atomic_raw_cas(u8* ptr, u8* expected, u8 desired, bool, MemoryOrder, MemoryOrder) noexcept {
    const u8 exp = *expected;
    const u8 prev = static_cast<u8>(
        _InterlockedCompareExchange8(reinterpret_cast<volatile char*>(ptr), static_cast<char>(desired), static_cast<char>(exp)));
    if (prev == exp) return true;
    *expected = prev;
    return false;
}

bool atomic_raw_cas(u16* ptr, u16* expected, u16 desired, bool, MemoryOrder, MemoryOrder) noexcept {
    const u16 exp = *expected;
    const u16 prev = static_cast<u16>(
        _InterlockedCompareExchange16(reinterpret_cast<volatile short*>(ptr), static_cast<short>(desired), static_cast<short>(exp)));
    if (prev == exp) return true;
    *expected = prev;
    return false;
}

bool atomic_raw_cas(u32* ptr, u32* expected, u32 desired, bool, MemoryOrder, MemoryOrder) noexcept {
    const u32 exp = *expected;
    const u32 prev = static_cast<u32>(
        _InterlockedCompareExchange(reinterpret_cast<volatile long*>(ptr), static_cast<long>(desired), static_cast<long>(exp)));
    if (prev == exp) return true;
    *expected = prev;
    return false;
}

bool atomic_raw_cas(u64* ptr, u64* expected, u64 desired, bool, MemoryOrder, MemoryOrder) noexcept {
    const u64 exp = *expected;
    const u64 prev = static_cast<u64>(
        _InterlockedCompareExchange64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(desired), static_cast<__int64>(exp)));
    if (prev == exp) return true;
    *expected = prev;
    return false;
}

u8 atomic_raw_fetch_add(u8* ptr, u8 delta, MemoryOrder) noexcept {
    return static_cast<u8>(_InterlockedExchangeAdd8(reinterpret_cast<volatile char*>(ptr), static_cast<char>(delta)));
}

u16 atomic_raw_fetch_add(u16* ptr, u16 delta, MemoryOrder) noexcept {
    return static_cast<u16>(_InterlockedExchangeAdd16(reinterpret_cast<volatile short*>(ptr), static_cast<short>(delta)));
}

u32 atomic_raw_fetch_add(u32* ptr, u32 delta, MemoryOrder) noexcept {
    return static_cast<u32>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), static_cast<long>(delta)));
}

u64 atomic_raw_fetch_add(u64* ptr, u64 delta, MemoryOrder) noexcept {
    return static_cast<u64>(_InterlockedExchangeAdd64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(delta)));
}

u8 atomic_raw_fetch_sub(u8* ptr, u8 delta, MemoryOrder order) noexcept {
    return atomic_raw_fetch_add(ptr, static_cast<u8>(0u - delta), order);
}

u16 atomic_raw_fetch_sub(u16* ptr, u16 delta, MemoryOrder order) noexcept {
    return atomic_raw_fetch_add(ptr, static_cast<u16>(0u - delta), order);
}

u32 atomic_raw_fetch_sub(u32* ptr, u32 delta, MemoryOrder order) noexcept {
    return atomic_raw_fetch_add(ptr, static_cast<u32>(0u - delta), order);
}

u64 atomic_raw_fetch_sub(u64* ptr, u64 delta, MemoryOrder order) noexcept {
    return atomic_raw_fetch_add(ptr, static_cast<u64>(0ULL - delta), order);
}

u8 atomic_raw_fetch_and(u8* ptr, u8 mask, MemoryOrder) noexcept {
    return static_cast<u8>(_InterlockedAnd8(reinterpret_cast<volatile char*>(ptr), static_cast<char>(mask)));
}

u16 atomic_raw_fetch_and(u16* ptr, u16 mask, MemoryOrder) noexcept {
    return static_cast<u16>(_InterlockedAnd16(reinterpret_cast<volatile short*>(ptr), static_cast<short>(mask)));
}

u32 atomic_raw_fetch_and(u32* ptr, u32 mask, MemoryOrder) noexcept {
    return static_cast<u32>(_InterlockedAnd(reinterpret_cast<volatile long*>(ptr), static_cast<long>(mask)));
}

u64 atomic_raw_fetch_and(u64* ptr, u64 mask, MemoryOrder) noexcept {
    return static_cast<u64>(_InterlockedAnd64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(mask)));
}

u8 atomic_raw_fetch_or(u8* ptr, u8 mask, MemoryOrder) noexcept {
    return static_cast<u8>(_InterlockedOr8(reinterpret_cast<volatile char*>(ptr), static_cast<char>(mask)));
}

u16 atomic_raw_fetch_or(u16* ptr, u16 mask, MemoryOrder) noexcept {
    return static_cast<u16>(_InterlockedOr16(reinterpret_cast<volatile short*>(ptr), static_cast<short>(mask)));
}

u32 atomic_raw_fetch_or(u32* ptr, u32 mask, MemoryOrder) noexcept {
    return static_cast<u32>(_InterlockedOr(reinterpret_cast<volatile long*>(ptr), static_cast<long>(mask)));
}

u64 atomic_raw_fetch_or(u64* ptr, u64 mask, MemoryOrder) noexcept {
    return static_cast<u64>(_InterlockedOr64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(mask)));
}

u8 atomic_raw_fetch_xor(u8* ptr, u8 mask, MemoryOrder) noexcept {
    return static_cast<u8>(_InterlockedXor8(reinterpret_cast<volatile char*>(ptr), static_cast<char>(mask)));
}

u16 atomic_raw_fetch_xor(u16* ptr, u16 mask, MemoryOrder) noexcept {
    return static_cast<u16>(_InterlockedXor16(reinterpret_cast<volatile short*>(ptr), static_cast<short>(mask)));
}

u32 atomic_raw_fetch_xor(u32* ptr, u32 mask, MemoryOrder) noexcept {
    return static_cast<u32>(_InterlockedXor(reinterpret_cast<volatile long*>(ptr), static_cast<long>(mask)));
}

u64 atomic_raw_fetch_xor(u64* ptr, u64 mask, MemoryOrder) noexcept {
    return static_cast<u64>(_InterlockedXor64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(mask)));
}

void atomic_raw_fence(MemoryOrder order) noexcept {
    if (order == MemoryOrder::Relaxed) return;
    if (order == MemoryOrder::SeqCst) {
        msvc_hw_barrier_seq_cst();
    } else {
        msvc_hw_barrier_acq_rel();
    }
}

} // namespace keel::detail

#endif
