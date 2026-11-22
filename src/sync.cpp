#include "keel/sync.h"

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define KEEL_SYNC_BACKEND_NONE 1
#elif defined(_WIN32)
#define KEEL_SYNC_BACKEND_WIN32 1
#include "win32_compat.h"
#else
#define KEEL_SYNC_BACKEND_POSIX 1
#include <cerrno>
#include <pthread.h>
#include <time.h>
#endif

namespace keel {

#if defined(KEEL_SYNC_BACKEND_NONE)

Mutex::Mutex() noexcept { (void)m_storage; }
Mutex::~Mutex() noexcept = default;
void Mutex::lock() noexcept {}
bool Mutex::try_lock() noexcept { return true; }
void Mutex::unlock() noexcept {}

SharedMutex::SharedMutex() noexcept { (void)m_storage; }
SharedMutex::~SharedMutex() noexcept = default;
void SharedMutex::lock() noexcept {}
bool SharedMutex::try_lock() noexcept { return true; }
void SharedMutex::unlock() noexcept {}
void SharedMutex::lock_shared() noexcept {}
bool SharedMutex::try_lock_shared() noexcept { return true; }
void SharedMutex::unlock_shared() noexcept {}

CondVar::CondVar() noexcept { (void)m_storage; }
CondVar::~CondVar() noexcept = default;
void CondVar::wait(Mutex&) noexcept {}
bool CondVar::wait_for_ms(Mutex&, u64) noexcept { return false; }
void CondVar::notify_one() noexcept {}
void CondVar::notify_all() noexcept {}

#elif defined(KEEL_SYNC_BACKEND_WIN32)

static_assert(sizeof(SRWLOCK) <= 64 && alignof(SRWLOCK) <= 8, "Mutex storage too small for SRWLOCK");
static_assert(sizeof(SRWLOCK) <= 256 && alignof(SRWLOCK) <= 8, "SharedMutex storage too small for SRWLOCK");
static_assert(sizeof(CONDITION_VARIABLE) <= 64 && alignof(CONDITION_VARIABLE) <= 8,
              "CondVar storage too small for CONDITION_VARIABLE");

namespace detail::sync_impl {
inline SRWLOCK* as_srw(u8* buf) noexcept { return reinterpret_cast<SRWLOCK*>(buf); }
inline CONDITION_VARIABLE* as_cv(u8* buf) noexcept { return reinterpret_cast<CONDITION_VARIABLE*>(buf); }
} // namespace detail::sync_impl

Mutex::Mutex() noexcept { InitializeSRWLock(detail::sync_impl::as_srw(m_storage)); }
Mutex::~Mutex() noexcept = default;
void Mutex::lock() noexcept { AcquireSRWLockExclusive(detail::sync_impl::as_srw(m_storage)); }
bool Mutex::try_lock() noexcept { return TryAcquireSRWLockExclusive(detail::sync_impl::as_srw(m_storage)) != 0; }
void Mutex::unlock() noexcept { ReleaseSRWLockExclusive(detail::sync_impl::as_srw(m_storage)); }

SharedMutex::SharedMutex() noexcept { InitializeSRWLock(detail::sync_impl::as_srw(m_storage)); }
SharedMutex::~SharedMutex() noexcept = default;
void SharedMutex::lock() noexcept { AcquireSRWLockExclusive(detail::sync_impl::as_srw(m_storage)); }
bool SharedMutex::try_lock() noexcept { return TryAcquireSRWLockExclusive(detail::sync_impl::as_srw(m_storage)) != 0; }
void SharedMutex::unlock() noexcept { ReleaseSRWLockExclusive(detail::sync_impl::as_srw(m_storage)); }
void SharedMutex::lock_shared() noexcept { AcquireSRWLockShared(detail::sync_impl::as_srw(m_storage)); }
bool SharedMutex::try_lock_shared() noexcept { return TryAcquireSRWLockShared(detail::sync_impl::as_srw(m_storage)) != 0; }
void SharedMutex::unlock_shared() noexcept { ReleaseSRWLockShared(detail::sync_impl::as_srw(m_storage)); }

CondVar::CondVar() noexcept { InitializeConditionVariable(detail::sync_impl::as_cv(m_storage)); }
CondVar::~CondVar() noexcept = default;
void CondVar::wait(Mutex& m) noexcept {
    SleepConditionVariableSRW(detail::sync_impl::as_cv(m_storage), detail::sync_impl::as_srw(m.m_storage), INFINITE, 0);
}
bool CondVar::wait_for_ms(Mutex& m, u64 timeout_ms) noexcept {
    const DWORD ms = (timeout_ms >= 0xFFFFFFFEULL) ? 0xFFFFFFFEu : as<DWORD>(timeout_ms);
    return SleepConditionVariableSRW(
               detail::sync_impl::as_cv(m_storage), detail::sync_impl::as_srw(m.m_storage), ms, 0) != 0;
}
void CondVar::notify_one() noexcept { WakeConditionVariable(detail::sync_impl::as_cv(m_storage)); }
void CondVar::notify_all() noexcept { WakeAllConditionVariable(detail::sync_impl::as_cv(m_storage)); }

#else // KEEL_SYNC_BACKEND_POSIX

static_assert(sizeof(pthread_mutex_t) <= 64 && alignof(pthread_mutex_t) <= 8,
              "Mutex storage too small for pthread_mutex_t");
static_assert(sizeof(pthread_rwlock_t) <= 256 && alignof(pthread_rwlock_t) <= 8,
              "SharedMutex storage too small for pthread_rwlock_t");
static_assert(sizeof(pthread_cond_t) <= 64 && alignof(pthread_cond_t) <= 8,
              "CondVar storage too small for pthread_cond_t");

namespace detail::sync_impl {
inline pthread_mutex_t* as_pmutex(u8* buf) noexcept { return reinterpret_cast<pthread_mutex_t*>(buf); }
inline pthread_rwlock_t* as_prwlock(u8* buf) noexcept { return reinterpret_cast<pthread_rwlock_t*>(buf); }
inline pthread_cond_t* as_pcond(u8* buf) noexcept { return reinterpret_cast<pthread_cond_t*>(buf); }
} // namespace detail::sync_impl

Mutex::Mutex() noexcept { pthread_mutex_init(detail::sync_impl::as_pmutex(m_storage), nullptr); }
Mutex::~Mutex() noexcept { pthread_mutex_destroy(detail::sync_impl::as_pmutex(m_storage)); }
void Mutex::lock() noexcept { pthread_mutex_lock(detail::sync_impl::as_pmutex(m_storage)); }
bool Mutex::try_lock() noexcept { return pthread_mutex_trylock(detail::sync_impl::as_pmutex(m_storage)) == 0; }
void Mutex::unlock() noexcept { pthread_mutex_unlock(detail::sync_impl::as_pmutex(m_storage)); }

SharedMutex::SharedMutex() noexcept { pthread_rwlock_init(detail::sync_impl::as_prwlock(m_storage), nullptr); }
SharedMutex::~SharedMutex() noexcept { pthread_rwlock_destroy(detail::sync_impl::as_prwlock(m_storage)); }
void SharedMutex::lock() noexcept { pthread_rwlock_wrlock(detail::sync_impl::as_prwlock(m_storage)); }
bool SharedMutex::try_lock() noexcept { return pthread_rwlock_trywrlock(detail::sync_impl::as_prwlock(m_storage)) == 0; }
void SharedMutex::unlock() noexcept { pthread_rwlock_unlock(detail::sync_impl::as_prwlock(m_storage)); }
void SharedMutex::lock_shared() noexcept { pthread_rwlock_rdlock(detail::sync_impl::as_prwlock(m_storage)); }
bool SharedMutex::try_lock_shared() noexcept {
    return pthread_rwlock_tryrdlock(detail::sync_impl::as_prwlock(m_storage)) == 0;
}
void SharedMutex::unlock_shared() noexcept { pthread_rwlock_unlock(detail::sync_impl::as_prwlock(m_storage)); }

CondVar::CondVar() noexcept {
#if !defined(__APPLE__) && defined(CLOCK_MONOTONIC)
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(detail::sync_impl::as_pcond(m_storage), &attr);
    pthread_condattr_destroy(&attr);
#else
    pthread_cond_init(detail::sync_impl::as_pcond(m_storage), nullptr);
#endif
}

CondVar::~CondVar() noexcept { pthread_cond_destroy(detail::sync_impl::as_pcond(m_storage)); }

void CondVar::wait(Mutex& m) noexcept {
    pthread_cond_wait(detail::sync_impl::as_pcond(m_storage), detail::sync_impl::as_pmutex(m.m_storage));
}

bool CondVar::wait_for_ms(Mutex& m, u64 timeout_ms) noexcept {
#if defined(__APPLE__)
    static constexpr u64 kMaxRelSec = 3153600000ULL; // 100 years cap to avoid time_t overflow
    const u64 sec = timeout_ms / 1000ULL;
    struct timespec rel{};
    rel.tv_sec = static_cast<time_t>((sec > kMaxRelSec) ? kMaxRelSec : sec);
    rel.tv_nsec = static_cast<long>((sec > kMaxRelSec) ? 0ULL : ((timeout_ms % 1000ULL) * 1000000ULL));
    const int rc = pthread_cond_timedwait_relative_np(
        detail::sync_impl::as_pcond(m_storage), detail::sync_impl::as_pmutex(m.m_storage), &rel);
    return rc != ETIMEDOUT;
#else
    struct timespec ts{};
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    static constexpr u64 kMaxAddSec = 3153600000ULL; // 100 years cap to avoid time_t overflow
    const u64 raw_sec = timeout_ms / 1000ULL;
    const u64 add_sec = (raw_sec > kMaxAddSec) ? kMaxAddSec : raw_sec;
    const u64 add_nsec = (raw_sec > kMaxAddSec) ? 0ULL : ((timeout_ms % 1000ULL) * 1000000ULL);
    const u64 total_nsec = static_cast<u64>(ts.tv_nsec) + add_nsec;
    ts.tv_sec += static_cast<time_t>(add_sec + total_nsec / 1000000000ULL);
    ts.tv_nsec = static_cast<long>(total_nsec % 1000000000ULL);
    const int rc = pthread_cond_timedwait(
        detail::sync_impl::as_pcond(m_storage), detail::sync_impl::as_pmutex(m.m_storage), &ts);
    return rc != ETIMEDOUT;
#endif
}

void CondVar::notify_one() noexcept { pthread_cond_signal(detail::sync_impl::as_pcond(m_storage)); }
void CondVar::notify_all() noexcept { pthread_cond_broadcast(detail::sync_impl::as_pcond(m_storage)); }

#endif

} // namespace keel
