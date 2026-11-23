#include "keel/os_signal.h"

#include "keel/atomic.h"
#include "keel/number.h"
#include "keel/sync.h"
#include "keel/thread.h"
#include "keel/time.h"

#if defined(__EMSCRIPTEN__)
#include <unistd.h>
#elif defined(_WIN32)
#include "win32_compat.h"
#include <csignal>
#include <cstdlib>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define KEEL_OS_SIGNAL_HAVE_EXECINFO 1
#endif
#endif

namespace keel {

namespace detail::os_sig_impl {

inline constexpr usize kMaxSlots = 32;

struct Slot {
    Atomic<u32> claimed{0};
    Atomic<u32> in_flight{0};
    Atomic<u32> mask{0};
    OsSignalCallback cb = nullptr;
    void* user_data = nullptr;
};

static Mutex g_ctrl_mutex;
static Slot g_slots[kMaxSlots];
static u32 g_sig_refcount[kOsSignalCount]{};
static Atomic<u32> g_pending_mask{0};
static Atomic<u64> g_sig_counts[kOsSignalCount]{};

// Crash handler state
static Mutex g_crash_mutex;
static Atomic<u32> g_crash_installed{0};
static Atomic<u32> g_in_crash{0};
static OsCrashOptions g_crash_opts{};

#if defined(_WIN32)
static Atomic<usize> g_win_event{0};
static bool g_win_ctrl_installed = false;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prev_uef = nullptr;
static void (*g_prev_sigabrt)(int) = nullptr;
#elif !defined(__EMSCRIPTEN__)
static Atomic<i32> g_pipe_rd{-1};
static Atomic<i32> g_pipe_wr{-1};
static struct sigaction g_prev_ctrl_sa[kOsSignalCount]{};
static bool g_have_prev_ctrl_sa[kOsSignalCount]{};

inline constexpr usize kCrashPosixCount = 5;
static const i32 kCrashPosixSignals[kCrashPosixCount] = {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL};
static struct sigaction g_prev_crash_sa[kCrashPosixCount]{};
static bool g_have_prev_crash_sa[kCrashPosixCount]{};
alignas(16) static u8 g_alt_stack_mem[65536];
static bool g_alt_stack_installed = false;
#endif

static void write_hex_ptr(const void* p) noexcept {
    os_signal_write_stderr("0x");
    char buf[32];
    os_signal_write_stderr(format_u64_to(buf, as<u64>(reinterpret_cast<usize>(p)), 16, false));
}

static void dispatch_control_signal(OsSignal sig) noexcept {
    const usize idx = as<usize>(sig);
    if (idx >= kOsSignalCount) return;
    const u32 bit = os_signal_bit(sig);

    g_pending_mask.fetch_or(bit, MemoryOrder::Release);
    g_sig_counts[idx].fetch_add(1u, MemoryOrder::Relaxed);

    for (usize i = 0; i < kMaxSlots; ++i) {
        Slot& s = g_slots[i];
        if (s.claimed.load(MemoryOrder::Acquire) == 0u) continue;
        s.in_flight.fetch_add(1u, MemoryOrder::AcqRel);
        if (s.claimed.load(MemoryOrder::Acquire) != 0u && (s.mask.load(MemoryOrder::Relaxed) & bit) != 0u) {
            if (s.cb) {
                s.cb(sig, s.user_data);
            }
        }
        s.in_flight.fetch_sub(1u, MemoryOrder::Release);
    }

#if defined(_WIN32)
    const usize ev = g_win_event.load(MemoryOrder::Acquire);
    if (ev != 0u) {
        (void)SetEvent(reinterpret_cast<HANDLE>(ev));
    }
#elif !defined(__EMSCRIPTEN__)
    const i32 wr = g_pipe_wr.load(MemoryOrder::Acquire);
    if (wr >= 0) {
        const u8 b = as<u8>(sig);
        const isize r = ::write(wr, &b, 1u);
        (void)r;
    }
#endif
}

#if defined(_WIN32)

static void ensure_win_event_locked() noexcept {
    if (g_win_event.load(MemoryOrder::Acquire) == 0u) {
        HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (h) {
            g_win_event.store(reinterpret_cast<usize>(h), MemoryOrder::Release);
        }
    }
}

static BOOL WINAPI win_console_ctrl_handler(DWORD ctrl_type) noexcept {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
            dispatch_control_signal(OsSignal::Interrupt);
            return TRUE;
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            dispatch_control_signal(OsSignal::Terminate);
            return TRUE;
        case CTRL_LOGOFF_EVENT:
            dispatch_control_signal(OsSignal::Hangup);
            return TRUE;
        default:
            return FALSE;
    }
}

static bool is_win_console_signal(OsSignal sig) noexcept {
    return sig == OsSignal::Interrupt || sig == OsSignal::Terminate || sig == OsSignal::Hangup;
}

static void update_win_ctrl_handler_locked() noexcept {
    const bool need = (g_sig_refcount[as<usize>(OsSignal::Interrupt)] > 0u) ||
                      (g_sig_refcount[as<usize>(OsSignal::Terminate)] > 0u) ||
                      (g_sig_refcount[as<usize>(OsSignal::Hangup)] > 0u);
    if (need && !g_win_ctrl_installed) {
        ensure_win_event_locked();
        if (SetConsoleCtrlHandler(&win_console_ctrl_handler, TRUE)) {
            g_win_ctrl_installed = true;
        }
    } else if (!need && g_win_ctrl_installed) {
        (void)SetConsoleCtrlHandler(&win_console_ctrl_handler, FALSE);
        g_win_ctrl_installed = false;
    }
}

static LONG WINAPI win_unhandled_exception_filter(EXCEPTION_POINTERS* ep) noexcept {
    if (g_in_crash.exchange(1u, MemoryOrder::AcqRel) != 0u) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    OsCrashInfo info{};
    if (ep && ep->ExceptionRecord) {
        const DWORD code = ep->ExceptionRecord->ExceptionCode;
        info.native_code = static_cast<i32>(code);
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:
                info.signal = OsCrashSignal::Segv;
                if (ep->ExceptionRecord->NumberParameters >= 2) {
                    info.fault_addr = reinterpret_cast<const void*>(ep->ExceptionRecord->ExceptionInformation[1]);
                }
                break;
            case EXCEPTION_IN_PAGE_ERROR:
                info.signal = OsCrashSignal::Bus;
                if (ep->ExceptionRecord->NumberParameters >= 2) {
                    info.fault_addr = reinterpret_cast<const void*>(ep->ExceptionRecord->ExceptionInformation[1]);
                }
                break;
            case EXCEPTION_STACK_OVERFLOW:
                info.signal = OsCrashSignal::StackOverflow;
                break;
            case EXCEPTION_ILLEGAL_INSTRUCTION:
            case EXCEPTION_PRIV_INSTRUCTION:
                info.signal = OsCrashSignal::Ill;
                break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            case EXCEPTION_FLT_INVALID_OPERATION:
            case EXCEPTION_FLT_OVERFLOW:
                info.signal = OsCrashSignal::Fpe;
                break;
            default:
                info.signal = OsCrashSignal::Segv;
                break;
        }
        info.pc = ep->ExceptionRecord->ExceptionAddress;
    }

    if (g_crash_opts.callback) {
        g_crash_opts.callback(info, g_crash_opts.user_data);
    }
    if (g_crash_opts.print_default_report) {
        os_crash_write_default_report(info, g_crash_opts.print_backtrace);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void win_sigabrt_handler(int signo) noexcept {
    if (g_in_crash.exchange(1u, MemoryOrder::AcqRel) != 0u) {
        std::signal(SIGABRT, SIG_DFL);
        std::raise(SIGABRT);
        std::_Exit(128 + signo);
    }
    OsCrashInfo info{};
    info.signal = OsCrashSignal::Abort;
    info.native_code = static_cast<i32>(signo);
    if (g_crash_opts.callback) {
        g_crash_opts.callback(info, g_crash_opts.user_data);
    }
    if (g_crash_opts.print_default_report) {
        os_crash_write_default_report(info, g_crash_opts.print_backtrace);
    }
    std::signal(SIGABRT, SIG_DFL);
    std::raise(SIGABRT);
    std::_Exit(128 + signo);
}

#elif !defined(__EMSCRIPTEN__) // POSIX

static i32 to_posix_signo(OsSignal sig) noexcept {
    switch (sig) {
        case OsSignal::Interrupt: return SIGINT;
        case OsSignal::Terminate: return SIGTERM;
        case OsSignal::Hangup:    return SIGHUP;
        case OsSignal::Pipe:      return SIGPIPE;
        case OsSignal::User1:     return SIGUSR1;
        case OsSignal::User2:     return SIGUSR2;
        case OsSignal::Child:     return SIGCHLD;
        case OsSignal::Winch:     return SIGWINCH;
    }
    return -1;
}

static bool from_posix_signo(i32 signo, OsSignal& out) noexcept {
    switch (signo) {
        case SIGINT:   out = OsSignal::Interrupt; return true;
        case SIGTERM:  out = OsSignal::Terminate; return true;
        case SIGHUP:   out = OsSignal::Hangup; return true;
        case SIGPIPE:  out = OsSignal::Pipe; return true;
        case SIGUSR1:  out = OsSignal::User1; return true;
        case SIGUSR2:  out = OsSignal::User2; return true;
        case SIGCHLD:  out = OsSignal::Child; return true;
        case SIGWINCH: out = OsSignal::Winch; return true;
        default:       return false;
    }
}

static void ensure_wake_pipe_locked() noexcept {
    if (g_pipe_rd.load(MemoryOrder::Acquire) >= 0) return;
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0) return;
    for (int k = 0; k < 2; ++k) {
        const int fd_flags = ::fcntl(fds[k], F_GETFD, 0);
        if (fd_flags >= 0) (void)::fcntl(fds[k], F_SETFD, fd_flags | FD_CLOEXEC);
        const int fl_flags = ::fcntl(fds[k], F_GETFL, 0);
        if (fl_flags >= 0) (void)::fcntl(fds[k], F_SETFL, fl_flags | O_NONBLOCK);
    }
    g_pipe_rd.store(fds[0], MemoryOrder::Release);
    g_pipe_wr.store(fds[1], MemoryOrder::Release);
}

static void posix_control_handler(int signo) noexcept {
    const int saved_errno = errno;
    OsSignal sig = OsSignal::Interrupt;
    if (from_posix_signo(signo, sig)) {
        dispatch_control_signal(sig);
    }
    errno = saved_errno;
}

static void install_posix_control_locked(OsSignal sig) noexcept {
    const usize idx = as<usize>(sig);
    const i32 signo = to_posix_signo(sig);
    if (signo <= 0) return;
    ensure_wake_pipe_locked();

    struct sigaction sa{};
    sa.sa_handler = &posix_control_handler;
    sigemptyset(&sa.sa_mask);
    // Do not set SA_RESTART for Interrupt/Terminate/Hangup/User1/User2 so blocking
    // I/O (like REPL fgets/read) wakes up promptly with EINTR.
    sa.sa_flags = (sig == OsSignal::Child || sig == OsSignal::Winch) ? SA_RESTART : 0;

    struct sigaction prev{};
    if (::sigaction(signo, &sa, &prev) == 0) {
        g_prev_ctrl_sa[idx] = prev;
        g_have_prev_ctrl_sa[idx] = true;
    }
}

static void restore_posix_control_locked(OsSignal sig) noexcept {
    const usize idx = as<usize>(sig);
    const i32 signo = to_posix_signo(sig);
    if (signo <= 0) return;
    if (g_have_prev_ctrl_sa[idx]) {
        (void)::sigaction(signo, &g_prev_ctrl_sa[idx], nullptr);
        g_have_prev_ctrl_sa[idx] = false;
    }
}

static const void* extract_posix_pc(void* uctx) noexcept {
    if (!uctx) return nullptr;
#if defined(__linux__) && defined(__x86_64__) && defined(REG_RIP)
    const auto* uc = static_cast<const ucontext_t*>(uctx);
    return reinterpret_cast<const void*>(static_cast<usize>(uc->uc_mcontext.gregs[REG_RIP]));
#elif defined(__linux__) && defined(__aarch64__)
    const auto* uc = static_cast<const ucontext_t*>(uctx);
    return reinterpret_cast<const void*>(static_cast<usize>(uc->uc_mcontext.pc));
#elif defined(__APPLE__) && defined(__x86_64__)
    const auto* uc = static_cast<const ucontext_t*>(uctx);
    return uc->uc_mcontext ? reinterpret_cast<const void*>(static_cast<usize>(uc->uc_mcontext->__ss.__rip)) : nullptr;
#elif defined(__APPLE__) && defined(__aarch64__)
    const auto* uc = static_cast<const ucontext_t*>(uctx);
    return uc->uc_mcontext ? reinterpret_cast<const void*>(static_cast<usize>(uc->uc_mcontext->__ss.__pc)) : nullptr;
#else
    return nullptr;
#endif
}

static void posix_crash_handler(int signo, siginfo_t* si, void* uctx) noexcept {
    if (g_in_crash.exchange(1u, MemoryOrder::AcqRel) != 0u) {
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        (void)::sigaction(signo, &dfl, nullptr);
        sigset_t set{};
        sigemptyset(&set);
        sigaddset(&set, signo);
        (void)::sigprocmask(SIG_UNBLOCK, &set, nullptr);
        (void)::raise(signo);
        ::_exit(128 + signo);
    }

    OsCrashInfo info{};
    info.native_code = static_cast<i32>(signo);
    info.fault_addr = si ? si->si_addr : nullptr;
    info.pc = extract_posix_pc(uctx);
    switch (signo) {
        case SIGSEGV: info.signal = OsCrashSignal::Segv; break;
        case SIGBUS:  info.signal = OsCrashSignal::Bus; break;
        case SIGABRT: info.signal = OsCrashSignal::Abort; break;
        case SIGFPE:  info.signal = OsCrashSignal::Fpe; break;
        case SIGILL:  info.signal = OsCrashSignal::Ill; break;
        default:      info.signal = OsCrashSignal::Segv; break;
    }

    if (g_crash_opts.callback) {
        g_crash_opts.callback(info, g_crash_opts.user_data);
    }
    if (g_crash_opts.print_default_report) {
        os_crash_write_default_report(info, g_crash_opts.print_backtrace);
    }

    // Restore OS default handler, unblock `signo`, and re-raise so core dumps
    // and parent WIFSIGNALED / WTERMSIG status are preserved accurately.
    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    (void)::sigaction(signo, &dfl, nullptr);

    sigset_t set{};
    sigemptyset(&set);
    sigaddset(&set, signo);
    (void)::sigprocmask(SIG_UNBLOCK, &set, nullptr);
    (void)::raise(signo);
    ::_exit(128 + signo);
}

#endif

} // namespace detail::os_sig_impl

// ============================================================================
// Control Signals Implementation
// ============================================================================

OsSignalGuard OsSignalGuard::subscribe(OsSignalMask mask, OsSignalCallback cb, void* user_data) noexcept {
    mask &= kOsSignalAll;
    if (mask == 0u) return OsSignalGuard{};

    using namespace detail::os_sig_impl;
    LockGuard<Mutex> lock(g_ctrl_mutex);

    u32 chosen = kInvalidSlot;
    for (usize i = 0; i < kMaxSlots; ++i) {
        if (g_slots[i].claimed.load(MemoryOrder::Relaxed) == 0u &&
            g_slots[i].in_flight.load(MemoryOrder::Acquire) == 0u) {
            chosen = as<u32>(i);
            break;
        }
    }
    if (chosen == kInvalidSlot) return OsSignalGuard{};

    Slot& s = g_slots[chosen];
    s.cb = cb;
    s.user_data = user_data;
    s.mask.store(mask, MemoryOrder::Relaxed);
    s.claimed.store(1u, MemoryOrder::Release);

    for (usize i = 0; i < kOsSignalCount; ++i) {
        if ((mask & (u32{1} << i)) == 0u) continue;
        const u32 prev_rc = g_sig_refcount[i]++;
        if (prev_rc == 0u) {
            const OsSignal sig = static_cast<OsSignal>(i);
#if defined(_WIN32)
            if (is_win_console_signal(sig)) {
                update_win_ctrl_handler_locked();
            }
#elif !defined(__EMSCRIPTEN__)
            install_posix_control_locked(sig);
#else
            (void)sig;
#endif
        }
    }

    return OsSignalGuard(chosen);
}

void OsSignalGuard::reset() noexcept {
    if (m_slot == kInvalidSlot) return;

    using namespace detail::os_sig_impl;
    const u32 idx = m_slot;
    m_slot = kInvalidSlot;

    LockGuard<Mutex> lock(g_ctrl_mutex);
    Slot& s = g_slots[idx];
    const u32 mask = s.mask.exchange(0u, MemoryOrder::AcqRel);
    s.claimed.store(0u, MemoryOrder::Release);

    // Wait lock-free for any in-flight signal callback on this slot to complete
    // before returning (guarantees caller can safely destroy `user_data`).
    while (s.in_flight.load(MemoryOrder::Acquire) != 0u) {
        thread_yield();
    }
    s.cb = nullptr;
    s.user_data = nullptr;

    for (usize i = 0; i < kOsSignalCount; ++i) {
        if ((mask & (u32{1} << i)) == 0u) continue;
        if (g_sig_refcount[i] > 0u) {
            --g_sig_refcount[i];
            if (g_sig_refcount[i] == 0u) {
                const OsSignal sig = static_cast<OsSignal>(i);
#if defined(_WIN32)
                if (is_win_console_signal(sig)) {
                    update_win_ctrl_handler_locked();
                }
#elif !defined(__EMSCRIPTEN__)
                restore_posix_control_locked(sig);
#else
                (void)sig;
#endif
            }
        }
    }
}

bool os_signal_pending(OsSignalMask mask) noexcept {
    return (detail::os_sig_impl::g_pending_mask.load(MemoryOrder::Acquire) & mask) != 0u;
}

Optional<OsSignal> os_signal_take(OsSignalMask mask) noexcept {
    mask &= kOsSignalAll;
    if (mask == 0u) return kNullOpt;

    auto& pending = detail::os_sig_impl::g_pending_mask;
    u32 cur = pending.load(MemoryOrder::Acquire);
    while (true) {
        const u32 matched = cur & mask;
        if (matched == 0u) return kNullOpt;
        usize chosen = 0;
        while (((matched >> chosen) & 1u) == 0u) ++chosen;
        const u32 next = cur & ~(u32{1} << chosen);
        if (pending.compare_exchange_weak(cur, next, MemoryOrder::AcqRel, MemoryOrder::Acquire)) {
            return static_cast<OsSignal>(chosen);
        }
    }
}

void os_signal_clear(OsSignalMask mask) noexcept {
    detail::os_sig_impl::g_pending_mask.fetch_and(~mask, MemoryOrder::AcqRel);
}

u64 os_signal_count(OsSignal sig) noexcept {
    const usize idx = as<usize>(sig);
    if (idx >= kOsSignalCount) return 0u;
    return detail::os_sig_impl::g_sig_counts[idx].load(MemoryOrder::Relaxed);
}

Optional<OsSignal> os_signal_wait(OsSignalMask mask) noexcept {
    return os_signal_wait_for_ms(mask, Limits<u64>::kMax);
}

Optional<OsSignal> os_signal_wait_for_ms(OsSignalMask mask, u64 timeout_ms) noexcept {
    mask &= kOsSignalAll;
    if (mask == 0u) return kNullOpt;
    if (auto got = os_signal_take(mask)) return got;
    if (timeout_ms == 0u) return kNullOpt;

#if defined(__EMSCRIPTEN__)
    return kNullOpt;
#elif defined(_WIN32)
    using namespace detail::os_sig_impl;
    {
        LockGuard<Mutex> lock(g_ctrl_mutex);
        ensure_win_event_locked();
    }
    const usize ev_raw = g_win_event.load(MemoryOrder::Acquire);
    if (ev_raw == 0u) return kNullOpt;
    HANDLE ev = reinterpret_cast<HANDLE>(ev_raw);

    const bool infinite = (timeout_ms == Limits<u64>::kMax);
    const u64 deadline = infinite ? 0u : (monotonic_ms() + timeout_ms);
    while (true) {
        if (auto got = os_signal_take(mask)) return got;
        (void)ResetEvent(ev);
        if (auto got = os_signal_take(mask)) return got;

        DWORD wait_ms = INFINITE;
        if (!infinite) {
            const u64 now = monotonic_ms();
            if (now >= deadline) return kNullOpt;
            const u64 rem = deadline - now;
            wait_ms = (rem > 0xFFFFFFFEULL) ? 0xFFFFFFFEu : static_cast<DWORD>(rem);
        }
        const DWORD rc = WaitForSingleObject(ev, wait_ms);
        if (auto got = os_signal_take(mask)) return got;
        if (rc == WAIT_TIMEOUT) return kNullOpt;
        if (rc != WAIT_OBJECT_0) return kNullOpt;
    }
#else // POSIX
    using namespace detail::os_sig_impl;
    {
        LockGuard<Mutex> lock(g_ctrl_mutex);
        ensure_wake_pipe_locked();
    }
    const i32 rd = g_pipe_rd.load(MemoryOrder::Acquire);
    if (rd < 0) return kNullOpt;

    const bool infinite = (timeout_ms == Limits<u64>::kMax);
    const u64 deadline = infinite ? 0u : (monotonic_ms() + timeout_ms);
    while (true) {
        if (auto got = os_signal_take(mask)) return got;

        int poll_ms = -1;
        if (!infinite) {
            const u64 now = monotonic_ms();
            if (now >= deadline) return kNullOpt;
            const u64 rem = deadline - now;
            poll_ms = (rem > as<u64>(Limits<i32>::kMax)) ? Limits<i32>::kMax : as<i32>(rem);
        }

        struct pollfd pfd{};
        pfd.fd = rd;
        pfd.events = POLLIN;
        const int rc = ::poll(&pfd, 1, poll_ms);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            u8 drain[64];
            while (::read(rd, drain, sizeof(drain)) > 0) {
            }
        }
        if (auto got = os_signal_take(mask)) return got;
        if (rc == 0 && !infinite) return kNullOpt;
        if (rc < 0 && errno != EINTR) return kNullOpt;
    }
#endif
}

Error os_signal_ignore(OsSignal sig) noexcept {
#if defined(__EMSCRIPTEN__) || defined(_WIN32)
    (void)sig;
    return Error::ok();
#else
    const i32 signo = detail::os_sig_impl::to_posix_signo(sig);
    if (signo <= 0) return Error{ErrorCode::InvalidArgument, 0};
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    if (::sigaction(signo, &sa, nullptr) != 0) {
        return Error{ErrorCode::Unknown, errno};
    }
    return Error::ok();
#endif
}

Error os_signal_restore_default(OsSignal sig) noexcept {
#if defined(__EMSCRIPTEN__) || defined(_WIN32)
    (void)sig;
    return Error::ok();
#else
    const i32 signo = detail::os_sig_impl::to_posix_signo(sig);
    if (signo <= 0) return Error{ErrorCode::InvalidArgument, 0};
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    if (::sigaction(signo, &sa, nullptr) != 0) {
        return Error{ErrorCode::Unknown, errno};
    }
    return Error::ok();
#endif
}

Error os_signal_raise(OsSignal sig) noexcept {
    if (as<usize>(sig) >= kOsSignalCount) return Error{ErrorCode::InvalidArgument, 0};
#if defined(__EMSCRIPTEN__) || defined(_WIN32)
    detail::os_sig_impl::dispatch_control_signal(sig);
    return Error::ok();
#else
    const i32 signo = detail::os_sig_impl::to_posix_signo(sig);
    if (signo <= 0) return Error{ErrorCode::InvalidArgument, 0};
    if (::raise(signo) != 0) {
        return Error{ErrorCode::Unknown, errno};
    }
    return Error::ok();
#endif
}

// ============================================================================
// Fatal Crash Signals Implementation
// ============================================================================

void os_signal_write_stderr(StringRef msg) noexcept {
    if (msg.empty() || !msg.data()) return;
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        (void)WriteFile(h, msg.data(), static_cast<DWORD>(msg.size()), &written, nullptr);
    }
#else
    const char* p = msg.data();
    usize rem = msg.size();
    while (rem > 0u) {
        const isize n = ::write(STDERR_FILENO, p, rem);
        if (n <= 0) break;
        p += as<usize>(n);
        rem -= as<usize>(n);
    }
#endif
}

void os_crash_write_default_report(const OsCrashInfo& info, bool include_backtrace) noexcept {
    char num_buf[32];
    os_signal_write_stderr("\n[keel] FATAL SIGNAL: ");
    os_signal_write_stderr(os_crash_signal_name(info.signal));
    os_signal_write_stderr(" (code=");
    os_signal_write_stderr(format_i64_to(num_buf, info.native_code));
    if (info.fault_addr != nullptr) {
        os_signal_write_stderr(", addr=");
        detail::os_sig_impl::write_hex_ptr(info.fault_addr);
    }
    if (info.pc != nullptr) {
        os_signal_write_stderr(", pc=");
        detail::os_sig_impl::write_hex_ptr(info.pc);
    }
    os_signal_write_stderr(")\n");

    if (!include_backtrace) return;

#if defined(_WIN32)
    void* frames[64];
    const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
    if (n > 0) {
        os_signal_write_stderr("Backtrace:\n");
        for (USHORT i = 0; i < n; ++i) {
            os_signal_write_stderr("  [");
            os_signal_write_stderr(format_u64_to(num_buf, i));
            os_signal_write_stderr("] ");
            detail::os_sig_impl::write_hex_ptr(frames[i]);
            os_signal_write_stderr("\n");
        }
    }
#elif defined(KEEL_OS_SIGNAL_HAVE_EXECINFO)
    void* frames[64];
    const int n = ::backtrace(frames, 64);
    if (n > 0) {
        os_signal_write_stderr("Backtrace:\n");
        ::backtrace_symbols_fd(frames, n, STDERR_FILENO);
    }
#endif
}

Error os_crash_install_handler(const OsCrashOptions& opts) noexcept {
#if defined(__EMSCRIPTEN__)
    (void)opts;
    return Error{ErrorCode::NotSupported, 0};
#elif defined(_WIN32)
    using namespace detail::os_sig_impl;
    LockGuard<Mutex> lock(g_crash_mutex);
    g_crash_opts = opts;
    g_in_crash.store(0u, MemoryOrder::Release);
    if (opts.install_alt_stack) {
        ULONG guarantee = 32768;
        (void)SetThreadStackGuarantee(&guarantee);
    }
    if (g_crash_installed.load(MemoryOrder::Acquire) == 0u) {
        g_prev_uef = SetUnhandledExceptionFilter(&win_unhandled_exception_filter);
        g_prev_sigabrt = std::signal(SIGABRT, &win_sigabrt_handler);
        g_crash_installed.store(1u, MemoryOrder::Release);
    }
    return Error::ok();
#else // POSIX
    using namespace detail::os_sig_impl;
    LockGuard<Mutex> lock(g_crash_mutex);
    g_crash_opts = opts;
    g_in_crash.store(0u, MemoryOrder::Release);

#if defined(KEEL_OS_SIGNAL_HAVE_EXECINFO)
    // Pre-warm backtrace() while heap/stack are healthy so glibc loads libgcc_s.so
    // before any crash occurs (avoiding dlopen/malloc inside a SIGSEGV handler).
    if (opts.print_backtrace) {
        void* warmup[4];
        (void)::backtrace(warmup, 4);
    }
#endif

    if (opts.install_alt_stack && !g_alt_stack_installed) {
        stack_t ss{};
        ss.ss_sp = g_alt_stack_mem;
        ss.ss_size = sizeof(g_alt_stack_mem);
        ss.ss_flags = 0;
        if (::sigaltstack(&ss, nullptr) == 0) {
            g_alt_stack_installed = true;
        }
    }

    if (g_crash_installed.load(MemoryOrder::Acquire) == 0u) {
        struct sigaction sa{};
        sa.sa_sigaction = &posix_crash_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND | (g_alt_stack_installed ? SA_ONSTACK : 0);

        for (usize i = 0; i < kCrashPosixCount; ++i) {
            struct sigaction prev{};
            if (::sigaction(kCrashPosixSignals[i], &sa, &prev) == 0) {
                g_prev_crash_sa[i] = prev;
                g_have_prev_crash_sa[i] = true;
            }
        }
        g_crash_installed.store(1u, MemoryOrder::Release);
    }
    return Error::ok();
#endif
}

void os_crash_uninstall_handler() noexcept {
#if defined(__EMSCRIPTEN__)
    return;
#elif defined(_WIN32)
    using namespace detail::os_sig_impl;
    LockGuard<Mutex> lock(g_crash_mutex);
    if (g_crash_installed.exchange(0u, MemoryOrder::AcqRel) != 0u) {
        (void)SetUnhandledExceptionFilter(g_prev_uef);
        g_prev_uef = nullptr;
        if (g_prev_sigabrt) {
            std::signal(SIGABRT, g_prev_sigabrt);
            g_prev_sigabrt = nullptr;
        }
        g_crash_opts = OsCrashOptions{};
    }
#else // POSIX
    using namespace detail::os_sig_impl;
    LockGuard<Mutex> lock(g_crash_mutex);
    if (g_crash_installed.exchange(0u, MemoryOrder::AcqRel) != 0u) {
        for (usize i = 0; i < kCrashPosixCount; ++i) {
            if (g_have_prev_crash_sa[i]) {
                (void)::sigaction(kCrashPosixSignals[i], &g_prev_crash_sa[i], nullptr);
                g_have_prev_crash_sa[i] = false;
            }
        }
        g_crash_opts = OsCrashOptions{};
    }
#endif
}

bool os_crash_handler_installed() noexcept {
    return detail::os_sig_impl::g_crash_installed.load(MemoryOrder::Acquire) != 0u;
}

} // namespace keel
