#pragma once

#include "keel/atomic.h"
#include "keel/core.h"
#include "keel/time.h"

namespace keel {

// Zero-allocation cross-platform mutual exclusion lock (`SRWLOCK` on Win32, `pthread_mutex_t` on POSIX,
// no-op on single-threaded WebAssembly).
class Mutex {
public:
    Mutex() noexcept;
    ~Mutex() noexcept;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    void lock() noexcept;
    [[nodiscard]] bool try_lock() noexcept;
    void unlock() noexcept;

private:
    friend class CondVar;
    static constexpr usize kStorageSize = 64;
    alignas(8) u8 m_storage[kStorageSize]{};
};

// Zero-allocation cross-platform reader-writer lock (`SRWLOCK` on Win32, `pthread_rwlock_t` on POSIX,
// no-op on single-threaded WebAssembly). Supports multiple concurrent readers or one exclusive writer.
class SharedMutex {
public:
    SharedMutex() noexcept;
    ~SharedMutex() noexcept;

    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;
    SharedMutex(SharedMutex&&) = delete;
    SharedMutex& operator=(SharedMutex&&) = delete;

    // Exclusive (writer) lock interface — compatible with LockGuard<SharedMutex> and UniqueLock<SharedMutex>.
    void lock() noexcept;
    [[nodiscard]] bool try_lock() noexcept;
    void unlock() noexcept;

    // Shared (reader) lock interface — compatible with SharedLock<SharedMutex>.
    void lock_shared() noexcept;
    [[nodiscard]] bool try_lock_shared() noexcept;
    void unlock_shared() noexcept;

private:
    // macOS arm64/x86_64 `pthread_rwlock_t` is 200 bytes; 256 bytes comfortably fits all POSIX/Win32 targets.
    static constexpr usize kStorageSize = 256;
    alignas(8) u8 m_storage[kStorageSize]{};
};

// Non-movable RAII scoped exclusive lock guard.
template <typename LockT = Mutex>
class LockGuard {
public:
    explicit LockGuard(LockT& lock) noexcept : m_lock(lock) { m_lock.lock(); }
    ~LockGuard() noexcept { m_lock.unlock(); }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    LockT& m_lock;
};

// Movable RAII exclusive lock supporting explicit unlock/relock and CondVar waits.
template <typename LockT = Mutex>
class UniqueLock {
public:
    UniqueLock() noexcept = default;
    explicit UniqueLock(LockT& lock) noexcept : m_lock(&lock), m_owns(true) { m_lock->lock(); }
    ~UniqueLock() noexcept {
        if (m_owns && m_lock) m_lock->unlock();
    }

    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;

    UniqueLock(UniqueLock&& other) noexcept : m_lock(other.m_lock), m_owns(other.m_owns) {
        other.m_lock = nullptr;
        other.m_owns = false;
    }

    UniqueLock& operator=(UniqueLock&& other) noexcept {
        if (this != &other) {
            if (m_owns && m_lock) m_lock->unlock();
            m_lock = other.m_lock;
            m_owns = other.m_owns;
            other.m_lock = nullptr;
            other.m_owns = false;
        }
        return *this;
    }

    void lock() noexcept {
        if (m_lock && !m_owns) {
            m_lock->lock();
            m_owns = true;
        }
    }

    [[nodiscard]] bool try_lock() noexcept {
        if (m_lock && !m_owns) {
            m_owns = m_lock->try_lock();
            return m_owns;
        }
        return m_owns;
    }

    void unlock() noexcept {
        if (m_lock && m_owns) {
            m_owns = false;
            m_lock->unlock();
        }
    }

    [[nodiscard]] bool owns_lock() const noexcept { return m_owns; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_owns; }
    [[nodiscard]] LockT* mutex() const noexcept { return m_lock; }

private:
    LockT* m_lock = nullptr;
    bool m_owns = false;
};

// Movable RAII shared (reader) lock for SharedMutex.
template <typename LockT = SharedMutex>
class SharedLock {
public:
    SharedLock() noexcept = default;
    explicit SharedLock(LockT& lock) noexcept : m_lock(&lock), m_owns(true) { m_lock->lock_shared(); }
    ~SharedLock() noexcept {
        if (m_owns && m_lock) m_lock->unlock_shared();
    }

    SharedLock(const SharedLock&) = delete;
    SharedLock& operator=(const SharedLock&) = delete;

    SharedLock(SharedLock&& other) noexcept : m_lock(other.m_lock), m_owns(other.m_owns) {
        other.m_lock = nullptr;
        other.m_owns = false;
    }

    SharedLock& operator=(SharedLock&& other) noexcept {
        if (this != &other) {
            if (m_owns && m_lock) m_lock->unlock_shared();
            m_lock = other.m_lock;
            m_owns = other.m_owns;
            other.m_lock = nullptr;
            other.m_owns = false;
        }
        return *this;
    }

    void lock() noexcept {
        if (m_lock && !m_owns) {
            m_lock->lock_shared();
            m_owns = true;
        }
    }

    [[nodiscard]] bool try_lock() noexcept {
        if (m_lock && !m_owns) {
            m_owns = m_lock->try_lock_shared();
            return m_owns;
        }
        return m_owns;
    }

    void unlock() noexcept {
        if (m_lock && m_owns) {
            m_owns = false;
            m_lock->unlock_shared();
        }
    }

    [[nodiscard]] bool owns_lock() const noexcept { return m_owns; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_owns; }
    [[nodiscard]] LockT* mutex() const noexcept { return m_lock; }

private:
    LockT* m_lock = nullptr;
    bool m_owns = false;
};

// Zero-allocation cross-platform condition variable (`CONDITION_VARIABLE` on Win32, `pthread_cond_t` on POSIX).
// Timed waits use monotonic clocks so NTP/wall-clock jumps never cause premature or stalled timeouts.
class CondVar {
public:
    CondVar() noexcept;
    ~CondVar() noexcept;

    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;
    CondVar(CondVar&&) = delete;
    CondVar& operator=(CondVar&&) = delete;

    void wait(Mutex& m) noexcept;

    void wait(UniqueLock<Mutex>& lock) noexcept {
        KEEL_ASSERT(lock.owns_lock() && lock.mutex() != nullptr);
        wait(*lock.mutex());
    }

    template <typename Pred>
    void wait(Mutex& m, Pred&& pred) {
        while (!pred()) {
            wait(m);
        }
    }

    template <typename Pred>
    void wait(UniqueLock<Mutex>& lock, Pred&& pred) {
        KEEL_ASSERT(lock.owns_lock() && lock.mutex() != nullptr);
        wait(*lock.mutex(), keel::forward<Pred>(pred));
    }

    // Waits up to `timeout_ms` milliseconds. Returns true if woken before timeout, false if timed out.
    [[nodiscard]] bool wait_for_ms(Mutex& m, u64 timeout_ms) noexcept;

    [[nodiscard]] bool wait_for_ms(UniqueLock<Mutex>& lock, u64 timeout_ms) noexcept {
        KEEL_ASSERT(lock.owns_lock() && lock.mutex() != nullptr);
        return wait_for_ms(*lock.mutex(), timeout_ms);
    }

    template <typename Pred>
    [[nodiscard]] bool wait_for_ms(Mutex& m, u64 timeout_ms, Pred&& pred) {
        if (pred()) return true;
        if (timeout_ms == 0) return false;
        const u64 start = monotonic_ns();
        const u64 max_add_ms = (Limits<u64>::kMax - start) / 1000000ULL;
        const u64 deadline_ns =
            (timeout_ms >= max_add_ms) ? Limits<u64>::kMax : (start + timeout_ms * 1000000ULL);
        while (!pred()) {
            const u64 now = monotonic_ns();
            if (now >= deadline_ns) return pred();
            const u64 diff = deadline_ns - now;
            const u64 rem_ms = (diff > Limits<u64>::kMax - 999999ULL) ? (diff / 1000000ULL) : ((diff + 999999ULL) / 1000000ULL);
            (void)wait_for_ms(m, rem_ms);
        }
        return true;
    }

    template <typename Pred>
    [[nodiscard]] bool wait_for_ms(UniqueLock<Mutex>& lock, u64 timeout_ms, Pred&& pred) {
        KEEL_ASSERT(lock.owns_lock() && lock.mutex() != nullptr);
        return wait_for_ms(*lock.mutex(), timeout_ms, keel::forward<Pred>(pred));
    }

    void notify_one() noexcept;
    void notify_all() noexcept;

private:
    static constexpr usize kStorageSize = 64;
    alignas(8) u8 m_storage[kStorageSize]{};
};

// Cross-platform manual-reset or auto-reset signaling event.
class Event {
public:
    explicit Event(bool manual_reset = true, bool initially_signaled = false) noexcept
        : m_manual_reset(manual_reset), m_signaled(initially_signaled) {}

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    void set() noexcept {
        LockGuard<Mutex> guard(m_mutex);
        m_signaled = true;
        if (m_manual_reset) {
            m_cv.notify_all();
        } else {
            m_cv.notify_one();
        }
    }

    void reset() noexcept {
        LockGuard<Mutex> guard(m_mutex);
        m_signaled = false;
    }

    [[nodiscard]] bool is_set() noexcept {
        LockGuard<Mutex> guard(m_mutex);
        return m_signaled;
    }

    void wait() noexcept {
        LockGuard<Mutex> guard(m_mutex);
        m_cv.wait(m_mutex, [this]() noexcept { return m_signaled; });
        if (!m_manual_reset) {
            m_signaled = false;
        }
    }

    [[nodiscard]] bool wait_for_ms(u64 timeout_ms) noexcept {
        LockGuard<Mutex> guard(m_mutex);
        const bool ok = m_cv.wait_for_ms(m_mutex, timeout_ms, [this]() noexcept { return m_signaled; });
        if (ok && !m_manual_reset) {
            m_signaled = false;
        }
        return ok;
    }

private:
    Mutex m_mutex;
    CondVar m_cv;
    bool m_manual_reset = true;
    bool m_signaled = false;
};

// Cross-platform zero-allocation counting semaphore with monotonic timed acquire.
class CountingSemaphore {
public:
    explicit CountingSemaphore(i64 initial_count = 0) noexcept
        : m_count((initial_count > 0) ? initial_count : 0) {}

    CountingSemaphore(const CountingSemaphore&) = delete;
    CountingSemaphore& operator=(const CountingSemaphore&) = delete;

    void release(i64 update = 1) noexcept {
        if (update <= 0) return;
        LockGuard<Mutex> guard(m_mutex);
        m_count += update;
        if (update == 1) {
            m_cv.notify_one();
        } else {
            m_cv.notify_all();
        }
    }

    void acquire() noexcept {
        LockGuard<Mutex> guard(m_mutex);
        m_cv.wait(m_mutex, [this]() noexcept { return m_count > 0; });
        --m_count;
    }

    [[nodiscard]] bool try_acquire() noexcept {
        LockGuard<Mutex> guard(m_mutex);
        if (m_count > 0) {
            --m_count;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool try_acquire_for_ms(u64 timeout_ms) noexcept {
        LockGuard<Mutex> guard(m_mutex);
        if (!m_cv.wait_for_ms(m_mutex, timeout_ms, [this]() noexcept { return m_count > 0; })) {
            return false;
        }
        --m_count;
        return true;
    }

private:
    Mutex m_mutex;
    CondVar m_cv;
    i64 m_count = 0;
};

// Thread-safe one-time initialization flag (lock-free fast path after completion).
class OnceFlag {
public:
    OnceFlag() noexcept = default;

    OnceFlag(const OnceFlag&) = delete;
    OnceFlag& operator=(const OnceFlag&) = delete;

    [[nodiscard]] bool is_done() const noexcept { return m_state.load(MemoryOrder::Acquire) == kDone; }

private:
    template <typename Fn> friend void call_once(OnceFlag& flag, Fn&& fn);

    static constexpr u32 kUninit = 0;
    static constexpr u32 kRunning = 1;
    static constexpr u32 kDone = 2;

    Atomic<u32> m_state{kUninit};
    Mutex m_mutex;
    CondVar m_cv;
};

// Invokes `fn()` exactly once across all threads sharing `flag`.
// If `fn()` throws an exception, another caller may retry initialization.
template <typename Fn>
void call_once(OnceFlag& flag, Fn&& fn) {
    if (flag.m_state.load(MemoryOrder::Acquire) == OnceFlag::kDone) {
        return;
    }
    UniqueLock<Mutex> lock(flag.m_mutex);
    for (;;) {
        const u32 st = flag.m_state.load(MemoryOrder::Acquire);
        if (st == OnceFlag::kDone) return;
        if (st == OnceFlag::kUninit) {
            flag.m_state.store(OnceFlag::kRunning, MemoryOrder::Release);
            lock.unlock();
#if defined(__cpp_exceptions)
            try {
                fn();
            } catch (...) {
                lock.lock();
                flag.m_state.store(OnceFlag::kUninit, MemoryOrder::Release);
                flag.m_cv.notify_all();
                throw;
            }
#else
            fn();
#endif
            lock.lock();
            flag.m_state.store(OnceFlag::kDone, MemoryOrder::Release);
            flag.m_cv.notify_all();
            return;
        }
        flag.m_cv.wait(lock, [&flag]() noexcept {
            return flag.m_state.load(MemoryOrder::Acquire) != OnceFlag::kRunning;
        });
    }
}

} // namespace keel
