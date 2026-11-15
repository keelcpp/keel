#include "keel/thread_pool.h"
#include "keel/exception.h"

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define KEEL_TP_BACKEND_NONE 1
#elif defined(_WIN32)
#define KEEL_TP_BACKEND_WIN32 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <intrin.h>
#include <windows.h>
#else
#define KEEL_TP_BACKEND_POSIX 1
#include <pthread.h>
#include <unistd.h>
#endif

namespace keel {

namespace detail::tp {

inline void cpu_pause() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif (defined(__clang__) || defined(__GNUC__)) && (defined(__x86_64__) || defined(__i386__))
    __builtin_ia32_pause();
#elif defined(_MSC_VER) && defined(_M_ARM64)
    __yield();
#elif (defined(__clang__) || defined(__GNUC__)) && defined(__aarch64__)
    __asm__ __volatile__("yield" ::: "memory");
#elif defined(__clang__) || defined(__GNUC__)
    __asm__ __volatile__("" ::: "memory");
#endif
}

#if !defined(KEEL_TP_BACKEND_NONE)

inline u32 load_acquire_u32(const u32* ptr) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
#else
    const u32 v = *static_cast<const volatile u32*>(ptr);
    MemoryBarrier();
    return v;
#endif
}

inline void store_release_u32(u32* ptr, u32 val) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE);
#else
    MemoryBarrier();
    *static_cast<volatile u32*>(ptr) = val;
    MemoryBarrier();
#endif
}

inline u32 sub_fetch_acq_rel_u32(u32* ptr) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return __atomic_sub_fetch(ptr, 1u, __ATOMIC_ACQ_REL);
#else
    return static_cast<u32>(_InterlockedDecrement(reinterpret_cast<volatile long*>(ptr)));
#endif
}

inline u64 load_acquire_u64(const u64* ptr) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
#else
    return static_cast<u64>(
        _InterlockedCompareExchange64(reinterpret_cast<volatile __int64*>(const_cast<u64*>(ptr)), 0, 0));
#endif
}

inline void store_release_u64(u64* ptr, u64 val) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE);
#else
    _InterlockedExchange64(reinterpret_cast<volatile __int64*>(ptr), static_cast<__int64>(val));
#endif
}

thread_local bool t_in_pool = false;

struct InPoolGuard {
    bool prev;
    InPoolGuard() noexcept : prev(t_in_pool) { t_in_pool = true; }
    ~InPoolGuard() noexcept { t_in_pool = prev; }
};

#if defined(KEEL_TP_BACKEND_WIN32)

struct Mutex {
    SRWLOCK lock = SRWLOCK_INIT;
    void acquire() noexcept { AcquireSRWLockExclusive(&lock); }
    void release() noexcept { ReleaseSRWLockExclusive(&lock); }
};

struct CondVar {
    CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
    void wait(Mutex& m) noexcept { SleepConditionVariableSRW(&cv, &m.lock, INFINITE, 0); }
    void notify_one() noexcept { WakeConditionVariable(&cv); }
    void notify_all() noexcept { WakeAllConditionVariable(&cv); }
};

using ThreadHandle = HANDLE;

#else // KEEL_TP_BACKEND_POSIX

struct Mutex {
    pthread_mutex_t lock;
    Mutex() noexcept { pthread_mutex_init(&lock, nullptr); }
    ~Mutex() noexcept { pthread_mutex_destroy(&lock); }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void acquire() noexcept { pthread_mutex_lock(&lock); }
    void release() noexcept { pthread_mutex_unlock(&lock); }
};

struct CondVar {
    pthread_cond_t cv;
    CondVar() noexcept { pthread_cond_init(&cv, nullptr); }
    ~CondVar() noexcept { pthread_cond_destroy(&cv); }
    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;
    void wait(Mutex& m) noexcept { pthread_cond_wait(&cv, &m.lock); }
    void notify_one() noexcept { pthread_cond_signal(&cv); }
    void notify_all() noexcept { pthread_cond_broadcast(&cv); }
};

using ThreadHandle = pthread_t;

#endif

struct LockGuard {
    Mutex& m;
    explicit LockGuard(Mutex& mu) noexcept : m(mu) { m.acquire(); }
    ~LockGuard() noexcept { m.release(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

#endif // !KEEL_TP_BACKEND_NONE

} // namespace detail::tp

u32 hardware_concurrency() noexcept {
#if defined(KEEL_TP_BACKEND_NONE)
    return 1u;
#elif defined(KEEL_TP_BACKEND_WIN32)
    SYSTEM_INFO sysinfo{};
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors > 0 ? static_cast<u32>(sysinfo.dwNumberOfProcessors) : 1u;
#else
    const long n = ::sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<u32>(n) : 1u;
#endif
}

struct ThreadPool::Impl {
#if !defined(KEEL_TP_BACKEND_NONE)
    struct WorkerArg {
        Impl* pool = nullptr;
        u32 worker_id = 0; // 1 .. num_workers - 1
    };

    Allocator* alloc = nullptr;
    detail::tp::ThreadHandle* threads = nullptr;
    WorkerArg* args = nullptr;
    u32 spawned_threads = 0;
    u32 allocated_threads = 0;

    detail::tp::Mutex submit_mu{};
    detail::tp::Mutex mu{};
    detail::tp::CondVar cv_start{};
    detail::tp::CondVar cv_done{};

    FunctionRef<void(usize, usize)> task_fn{};
    usize task_count = 0;
    u32 active = 0;
    u32 remaining = 0;
    u64 epoch = 0;
    u32 shutdown = 0;

#if KEEL_HAS_EXCEPTIONS
    std::exception_ptr captured_ex{};
#endif

    void worker_loop(u32 id) noexcept {
        u64 seen_epoch = 0;
        for (;;) {
            FunctionRef<void(usize, usize)> fn{};
            usize n = 0;
            u32 k = 0;

            for (u32 spin = 0; spin < 2000; ++spin) {
                if (detail::tp::load_acquire_u32(&shutdown) != 0 ||
                    detail::tp::load_acquire_u64(&epoch) != seen_epoch) {
                    break;
                }
                detail::tp::cpu_pause();
            }

            {
                detail::tp::LockGuard lk(mu);
                while (detail::tp::load_acquire_u32(&shutdown) == 0 &&
                       detail::tp::load_acquire_u64(&epoch) == seen_epoch) {
                    cv_start.wait(mu);
                }
                if (detail::tp::load_acquire_u32(&shutdown) != 0) {
                    return;
                }
                seen_epoch = epoch;
                if (id >= active) {
                    continue;
                }
                fn = task_fn;
                n = task_count;
                k = active;
            }

            const usize chunk = (n + static_cast<usize>(k) - 1) / static_cast<usize>(k);
            const usize start = static_cast<usize>(id) * chunk;
            const usize end = (start + chunk < n) ? (start + chunk) : n;

            if (start < end) {
                detail::tp::InPoolGuard in_pool_guard;
#if KEEL_HAS_EXCEPTIONS
                try {
                    fn(start, end);
                } catch (...) {
                    detail::tp::LockGuard lk(mu);
                    if (!captured_ex) {
                        captured_ex = std::current_exception();
                    }
                }
#else
                fn(start, end);
#endif
            }

            if (detail::tp::sub_fetch_acq_rel_u32(&remaining) == 0u) {
                detail::tp::LockGuard lk(mu);
                cv_done.notify_one();
            }
        }
    }

#if defined(KEEL_TP_BACKEND_WIN32)
    static DWORD WINAPI thread_entry(LPVOID param) noexcept {
        auto* warg = static_cast<WorkerArg*>(param);
        warg->pool->worker_loop(warg->worker_id);
        return 0;
    }
#else
    static void* thread_entry(void* param) noexcept {
        auto* warg = static_cast<WorkerArg*>(param);
        warg->pool->worker_loop(warg->worker_id);
        return nullptr;
    }
#endif
#endif // !KEEL_TP_BACKEND_NONE
};

ThreadPool& ThreadPool::global() {
    static ThreadPool s_instance(0, default_allocator());
    return s_instance;
}

ThreadPool::ThreadPool(u32 num_workers, Allocator* alloc)
    : m_alloc(alloc ? alloc : default_allocator()) {
    const u32 target = (num_workers == 0) ? hardware_concurrency() : num_workers;
#if defined(KEEL_TP_BACKEND_NONE)
    (void)target;
    m_num_workers = 1;
#else
    if (target <= 1) {
        m_num_workers = 1;
        return;
    }

    const u32 bg_count = target - 1;
    void* mem = m_alloc->try_alloc_bytes(sizeof(Impl), alignof(Impl));
    if (!mem) {
        m_num_workers = 1;
        return;
    }
    m_impl = ::new (mem) Impl{};
    m_impl->alloc = m_alloc;

    m_impl->threads = static_cast<detail::tp::ThreadHandle*>(
        m_alloc->try_alloc_bytes(sizeof(detail::tp::ThreadHandle) * bg_count, alignof(detail::tp::ThreadHandle)));
    m_impl->args = static_cast<Impl::WorkerArg*>(
        m_alloc->try_alloc_bytes(sizeof(Impl::WorkerArg) * bg_count, alignof(Impl::WorkerArg)));

    if (!m_impl->threads || !m_impl->args) {
        if (m_impl->threads) {
            m_alloc->free_bytes(m_impl->threads, sizeof(detail::tp::ThreadHandle) * bg_count,
                                alignof(detail::tp::ThreadHandle));
        }
        if (m_impl->args) {
            m_alloc->free_bytes(m_impl->args, sizeof(Impl::WorkerArg) * bg_count, alignof(Impl::WorkerArg));
        }
        m_impl->~Impl();
        m_alloc->free_bytes(m_impl, sizeof(Impl), alignof(Impl));
        m_impl = nullptr;
        m_num_workers = 1;
        return;
    }

    u32 spawned = 0;
    for (u32 i = 0; i < bg_count; ++i) {
        m_impl->args[i].pool = m_impl;
        m_impl->args[i].worker_id = i + 1;
#if defined(KEEL_TP_BACKEND_WIN32)
        HANDLE h = CreateThread(nullptr, 0, &Impl::thread_entry, &m_impl->args[i], 0, nullptr);
        if (!h) break;
        m_impl->threads[i] = h;
#else
        if (pthread_create(&m_impl->threads[i], nullptr, &Impl::thread_entry, &m_impl->args[i]) != 0) {
            break;
        }
#endif
        ++spawned;
    }

    m_impl->spawned_threads = spawned;
    m_impl->allocated_threads = bg_count;
    if (spawned == 0) {
        m_alloc->free_bytes(m_impl->threads, sizeof(detail::tp::ThreadHandle) * bg_count,
                            alignof(detail::tp::ThreadHandle));
        m_alloc->free_bytes(m_impl->args, sizeof(Impl::WorkerArg) * bg_count, alignof(Impl::WorkerArg));
        m_impl->~Impl();
        m_alloc->free_bytes(m_impl, sizeof(Impl), alignof(Impl));
        m_impl = nullptr;
        m_num_workers = 1;
        return;
    }

    m_num_workers = spawned + 1;
#endif
}

ThreadPool::~ThreadPool() noexcept {
#if !defined(KEEL_TP_BACKEND_NONE)
    if (!m_impl) return;
    {
        detail::tp::LockGuard lk(m_impl->mu);
        detail::tp::store_release_u32(&m_impl->shutdown, 1u);
        detail::tp::store_release_u64(&m_impl->epoch, m_impl->epoch + 1);
    }
    m_impl->cv_start.notify_all();

    for (u32 i = 0; i < m_impl->spawned_threads; ++i) {
#if defined(KEEL_TP_BACKEND_WIN32)
        WaitForSingleObject(m_impl->threads[i], INFINITE);
        CloseHandle(m_impl->threads[i]);
#else
        pthread_join(m_impl->threads[i], nullptr);
#endif
    }

    const u32 alloc_slots = m_impl->allocated_threads;
    m_alloc->free_bytes(m_impl->threads, sizeof(detail::tp::ThreadHandle) * alloc_slots,
                        alignof(detail::tp::ThreadHandle));
    m_alloc->free_bytes(m_impl->args, sizeof(Impl::WorkerArg) * alloc_slots, alignof(Impl::WorkerArg));
    m_impl->~Impl();
    m_alloc->free_bytes(m_impl, sizeof(Impl), alignof(Impl));
    m_impl = nullptr;
#endif
}

void ThreadPool::run(usize count, FunctionRef<void(usize begin, usize end)> fn, u32 max_workers) {
    if (count == 0 || !fn) return;

#if defined(KEEL_TP_BACKEND_NONE)
    (void)max_workers;
    fn(0, count);
#else
    if (detail::tp::t_in_pool) {
        fn(0, count);
        return;
    }

    u32 k = m_num_workers;
    if (max_workers > 0 && max_workers < k) {
        k = max_workers;
    }
    if (static_cast<usize>(k) > count) {
        k = static_cast<u32>(count);
    }
    if (k <= 1 || !m_impl) {
        fn(0, count);
        return;
    }

    m_impl->submit_mu.acquire();

    {
        detail::tp::LockGuard lk(m_impl->mu);
        m_impl->task_fn = fn;
        m_impl->task_count = count;
        m_impl->active = k;
        detail::tp::store_release_u32(&m_impl->remaining, k - 1);
        detail::tp::store_release_u64(&m_impl->epoch, m_impl->epoch + 1);
    }
    m_impl->cv_start.notify_all();

    const usize chunk = (count + static_cast<usize>(k) - 1) / static_cast<usize>(k);
    const usize end0 = (chunk < count) ? chunk : count;
    if (end0 > 0) {
        detail::tp::InPoolGuard in_pool_guard;
#if KEEL_HAS_EXCEPTIONS
        try {
            fn(0, end0);
        } catch (...) {
            detail::tp::LockGuard lk(m_impl->mu);
            if (!m_impl->captured_ex) {
                m_impl->captured_ex = std::current_exception();
            }
        }
#else
        fn(0, end0);
#endif
    }

    for (u32 spin = 0; spin < 2000; ++spin) {
        if (detail::tp::load_acquire_u32(&m_impl->remaining) == 0u) break;
        detail::tp::cpu_pause();
    }

#if KEEL_HAS_EXCEPTIONS
    std::exception_ptr ex{};
#endif
    {
        detail::tp::LockGuard lk(m_impl->mu);
        while (detail::tp::load_acquire_u32(&m_impl->remaining) != 0u) {
            m_impl->cv_done.wait(m_impl->mu);
        }
        m_impl->active = 0;
        m_impl->task_fn = nullptr;
        m_impl->task_count = 0;
#if KEEL_HAS_EXCEPTIONS
        ex = static_cast<std::exception_ptr&&>(m_impl->captured_ex);
        m_impl->captured_ex = nullptr;
#endif
    }

    m_impl->submit_mu.release();

#if KEEL_HAS_EXCEPTIONS
    if (ex) {
        std::rethrow_exception(ex);
    }
#endif
#endif
}

} // namespace keel
