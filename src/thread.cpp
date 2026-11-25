#include "keel/thread.h"
#include "keel/small_array.h"

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define KEEL_THREAD_BACKEND_NONE 1
#elif defined(_WIN32)
#define KEEL_THREAD_BACKEND_WIN32 1
#include "win32_compat.h"
#else
#define KEEL_THREAD_BACKEND_POSIX 1
#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace keel {

namespace detail {
#if defined(_WIN32)
Error win32_error_to_keel(DWORD err) noexcept;
#elif !defined(KEEL_THREAD_BACKEND_NONE)
Error errno_to_keel(int err) noexcept;
#endif
} // namespace detail

#if defined(KEEL_THREAD_BACKEND_NONE)

u64 thread_id() noexcept { return 1ULL; }
void thread_yield() noexcept {}
void thread_set_name(StringRef) noexcept {}

Result<Thread> Thread::spawn(Function<void()>, StringRef, Allocator*) {
    return Error{ErrorCode::NotSupported, 0};
}

void Thread::join() noexcept { m_joinable = false; }
void Thread::detach() noexcept { m_joinable = false; }

#else

namespace detail::thread_impl {

struct LaunchState {
    Function<void()> fn;
    SmallString<64> name;
    Allocator* alloc = nullptr;
};

inline void run_and_destroy(LaunchState* state) noexcept {
    if (!state) return;
    if (!state->name.empty()) {
        thread_set_name(state->name.as_ref());
    }
    if (state->fn) {
        state->fn();
    }
    Allocator* alloc = state->alloc;
    state->~LaunchState();
    alloc->free_bytes(state, sizeof(LaunchState), alignof(LaunchState));
}

#if defined(KEEL_THREAD_BACKEND_WIN32)
inline DWORD WINAPI win32_entry(LPVOID arg) noexcept {
    run_and_destroy(static_cast<LaunchState*>(arg));
    return 0;
}
#else
inline void* posix_entry(void* arg) noexcept {
    run_and_destroy(static_cast<LaunchState*>(arg));
    return nullptr;
}
#endif

} // namespace detail::thread_impl

u64 thread_id() noexcept {
#if defined(KEEL_THREAD_BACKEND_WIN32)
    return static_cast<u64>(GetCurrentThreadId());
#elif defined(__APPLE__)
    u64 tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#elif defined(__linux__) && defined(SYS_gettid)
    return static_cast<u64>(::syscall(SYS_gettid));
#else
    u64 tid = 0;
    const pthread_t self = pthread_self();
    const usize copy_n = (sizeof(self) < sizeof(tid)) ? sizeof(self) : sizeof(tid);
    mem_copy(&tid, &self, copy_n);
    return tid;
#endif
}

void thread_yield() noexcept {
#if defined(KEEL_THREAD_BACKEND_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

void thread_set_name(StringRef name) noexcept {
    if (name.empty()) return;
#if defined(KEEL_THREAD_BACKEND_WIN32)
    using PfnSetThreadDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const PfnSetThreadDescription s_set_desc = []() noexcept -> PfnSetThreadDescription {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        return k32 ? reinterpret_cast<PfnSetThreadDescription>(reinterpret_cast<void*>(GetProcAddress(k32, "SetThreadDescription"))) : nullptr;
    }();
    if (s_set_desc) {
        SmallArray<wchar_t, 64> wname;
        const int needed = MultiByteToWideChar(CP_UTF8, 0, name.data(), as<int>(name.size()), nullptr, 0);
        if (needed > 0) {
            wname.resize(as<usize>(needed) + 1u, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, name.data(), as<int>(name.size()), wname.data(), needed);
            (void)s_set_desc(GetCurrentThread(), wname.data());
        }
    }
#elif defined(__APPLE__)
    char buf[64]{};
    const usize n = (name.size() < sizeof(buf) - 1u) ? name.size() : (sizeof(buf) - 1u);
    mem_copy(buf, name.data(), n);
    buf[n] = '\0';
    (void)pthread_setname_np(buf);
#elif defined(__linux__)
    // Linux limits thread names to 16 bytes including null terminator
    char buf[16]{};
    const usize n = (name.size() < sizeof(buf) - 1u) ? name.size() : (sizeof(buf) - 1u);
    mem_copy(buf, name.data(), n);
    buf[n] = '\0';
    (void)pthread_setname_np(pthread_self(), buf);
#else
    (void)name;
#endif
}

Result<Thread> Thread::spawn(Function<void()> fn, StringRef name, Allocator* alloc) {
    if (!fn) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    Allocator* a = alloc ? alloc : default_allocator();
    void* mem = a->try_alloc_bytes(sizeof(detail::thread_impl::LaunchState), alignof(detail::thread_impl::LaunchState));
    if (!mem) {
        return Error{ErrorCode::OutOfRange, 0};
    }
    auto* state = new (mem) detail::thread_impl::LaunchState{
        keel::move(fn),
        SmallString<64>(name),
        a
    };

    Thread out;
#if defined(KEEL_THREAD_BACKEND_WIN32)
    HANDLE h = CreateThread(nullptr, 0, &detail::thread_impl::win32_entry, state, 0, nullptr);
    if (!h) {
        const Error err = detail::win32_error_to_keel(GetLastError());
        state->~LaunchState();
        a->free_bytes(state, sizeof(detail::thread_impl::LaunchState), alignof(detail::thread_impl::LaunchState));
        return err;
    }
    out.m_handle = static_cast<u64>(reinterpret_cast<usize>(h));
    out.m_joinable = true;
    return out;
#else
    static_assert(sizeof(pthread_t) <= sizeof(u64), "Thread handle storage too small for pthread_t");
    pthread_t pt{};
    const int rc = pthread_create(&pt, nullptr, &detail::thread_impl::posix_entry, state);
    if (rc != 0) {
        const Error err = detail::errno_to_keel(rc);
        state->~LaunchState();
        a->free_bytes(state, sizeof(detail::thread_impl::LaunchState), alignof(detail::thread_impl::LaunchState));
        return err;
    }
    u64 handle_bits = 0;
    mem_copy(&handle_bits, &pt, sizeof(pt));
    out.m_handle = handle_bits;
    out.m_joinable = true;
    return out;
#endif
}

void Thread::join() noexcept {
    if (!m_joinable) return;
    m_joinable = false;
#if defined(KEEL_THREAD_BACKEND_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<usize>(m_handle));
    if (h) {
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
    }
#else
    pthread_t pt{};
    mem_copy(&pt, &m_handle, sizeof(pt));
    (void)pthread_join(pt, nullptr);
#endif
    m_handle = 0;
}

void Thread::detach() noexcept {
    if (!m_joinable) return;
    m_joinable = false;
#if defined(KEEL_THREAD_BACKEND_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<usize>(m_handle));
    if (h) {
        CloseHandle(h);
    }
#else
    pthread_t pt{};
    mem_copy(&pt, &m_handle, sizeof(pt));
    (void)pthread_detach(pt);
#endif
    m_handle = 0;
}

#endif

} // namespace keel
