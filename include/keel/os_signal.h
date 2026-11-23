#pragma once

#include "keel/core.h"
#include "keel/error.h"
#include "keel/optional.h"
#include "keel/ref.h"

namespace keel {

// ============================================================================
// Part 1: Control Signals (process stays alive or performs graceful shutdown)
// ============================================================================

enum class OsSignal : u8 {
    Interrupt = 0, // SIGINT / CTRL_C_EVENT
    Terminate = 1, // SIGTERM / CTRL_BREAK_EVENT / CTRL_CLOSE_EVENT / CTRL_SHUTDOWN_EVENT
    Hangup    = 2, // SIGHUP / CTRL_LOGOFF_EVENT
    Pipe      = 3, // SIGPIPE (POSIX; no-op on Win32/WASM)
    User1     = 4, // SIGUSR1 (POSIX)
    User2     = 5, // SIGUSR2 (POSIX)
    Child     = 6, // SIGCHLD (POSIX)
    Winch     = 7, // SIGWINCH (POSIX terminal resize)
};

inline constexpr usize kOsSignalCount = 8;

using OsSignalMask = u32;

[[nodiscard]] constexpr OsSignalMask os_signal_bit(OsSignal sig) noexcept {
    return u32{1} << as<u32>(sig);
}

inline constexpr OsSignalMask kOsSignalNone     = 0u;
inline constexpr OsSignalMask kOsSignalShutdown = os_signal_bit(OsSignal::Interrupt) | os_signal_bit(OsSignal::Terminate);
inline constexpr OsSignalMask kOsSignalAll      = (u32{1} << kOsSignalCount) - 1u;

[[nodiscard]] constexpr StringRef os_signal_name(OsSignal sig) noexcept {
    switch (sig) {
        case OsSignal::Interrupt: return "SIGINT";
        case OsSignal::Terminate: return "SIGTERM";
        case OsSignal::Hangup:    return "SIGHUP";
        case OsSignal::Pipe:      return "SIGPIPE";
        case OsSignal::User1:     return "SIGUSR1";
        case OsSignal::User2:     return "SIGUSR2";
        case OsSignal::Child:     return "SIGCHLD";
        case OsSignal::Winch:     return "SIGWINCH";
    }
    return "UNKNOWN";
}

// Async-signal-safe callback invoked directly from OS signal context.
// MUST NOT allocate, lock a Mutex, throw, or call stdio. Atomic stores/loads and
// `os_signal_write_stderr` are safe.
using OsSignalCallback = void (*)(OsSignal sig, void* user_data) noexcept;

// Move-only RAII subscription to one or more OS control signals.
// Automatically installs the OS handler on first subscriber and restores the previous
// OS handler when the last subscriber for a signal releases. Destructor waits lock-free
// for any in-flight callback on this slot before returning, preventing use-after-free on `user_data`.
class OsSignalGuard {
public:
    OsSignalGuard() noexcept = default;
    ~OsSignalGuard() noexcept { reset(); }

    OsSignalGuard(OsSignalGuard&& other) noexcept : m_slot(other.m_slot) { other.m_slot = kInvalidSlot; }
    OsSignalGuard& operator=(OsSignalGuard&& other) noexcept {
        if (this != &other) {
            reset();
            m_slot = other.m_slot;
            other.m_slot = kInvalidSlot;
        }
        return *this;
    }

    OsSignalGuard(const OsSignalGuard&) = delete;
    OsSignalGuard& operator=(const OsSignalGuard&) = delete;

    [[nodiscard]] static OsSignalGuard subscribe(OsSignalMask mask, OsSignalCallback cb = nullptr,
                                                 void* user_data = nullptr) noexcept;
    [[nodiscard]] static OsSignalGuard subscribe(OsSignal sig, OsSignalCallback cb = nullptr,
                                                 void* user_data = nullptr) noexcept {
        return subscribe(os_signal_bit(sig), cb, user_data);
    }

    [[nodiscard]] bool is_active() const noexcept { return m_slot != kInvalidSlot; }
    explicit operator bool() const noexcept { return is_active(); }

    void reset() noexcept;

private:
    static inline constexpr u32 kInvalidSlot = Limits<u32>::kMax;
    explicit OsSignalGuard(u32 slot) noexcept : m_slot(slot) {}
    u32 m_slot = kInvalidSlot;
};

// Non-blocking lock-free polling of pending control signals.
[[nodiscard]] bool os_signal_pending(OsSignalMask mask = kOsSignalAll) noexcept;
[[nodiscard]] Optional<OsSignal> os_signal_take(OsSignalMask mask = kOsSignalAll) noexcept;
void os_signal_clear(OsSignalMask mask = kOsSignalAll) noexcept;
[[nodiscard]] u64 os_signal_count(OsSignal sig) noexcept;

// Blocking wait without busy-polling (POSIX self-pipe + poll, Win32 Event + WaitForSingleObject).
// Returns the first pending/received signal matching `mask`, or `kNullOpt` on timeout / unsupported.
[[nodiscard]] Optional<OsSignal> os_signal_wait(OsSignalMask mask = kOsSignalShutdown) noexcept;
[[nodiscard]] Optional<OsSignal> os_signal_wait_for_ms(OsSignalMask mask, u64 timeout_ms) noexcept;

// Ignores `sig` (e.g. `os_signal_ignore(OsSignal::Pipe)` for SIGPIPE on POSIX; no-op on Win32/WASM).
Error os_signal_ignore(OsSignal sig) noexcept;
// Restores OS default disposition for `sig`.
Error os_signal_restore_default(OsSignal sig) noexcept;
// Sends `sig` to the current process (POSIX `raise`, Win32/WASM in-process dispatch).
Error os_signal_raise(OsSignal sig) noexcept;

// ============================================================================
// Part 2: Fatal Crash Signals (diagnostic-only, terminates with OS default)
// ============================================================================

enum class OsCrashSignal : u8 {
    Segv          = 0, // SIGSEGV / EXCEPTION_ACCESS_VIOLATION
    Bus           = 1, // SIGBUS  / EXCEPTION_IN_PAGE_ERROR
    Abort         = 2, // SIGABRT
    Fpe           = 3, // SIGFPE  / EXCEPTION_INT_DIVIDE_BY_ZERO, FLT_*
    Ill           = 4, // SIGILL  / EXCEPTION_ILLEGAL_INSTRUCTION
    StackOverflow = 5, // SIGSEGV on stack guard page / EXCEPTION_STACK_OVERFLOW
};

[[nodiscard]] constexpr StringRef os_crash_signal_name(OsCrashSignal sig) noexcept {
    switch (sig) {
        case OsCrashSignal::Segv:          return "SIGSEGV";
        case OsCrashSignal::Bus:           return "SIGBUS";
        case OsCrashSignal::Abort:         return "SIGABRT";
        case OsCrashSignal::Fpe:           return "SIGFPE";
        case OsCrashSignal::Ill:           return "SIGILL";
        case OsCrashSignal::StackOverflow: return "STACK_OVERFLOW";
    }
    return "UNKNOWN";
}

struct OsCrashInfo {
    OsCrashSignal signal = OsCrashSignal::Segv;
    i32 native_code = 0;        // POSIX signo or Win32 ExceptionCode
    const void* fault_addr = nullptr;
    const void* pc = nullptr;   // Best-effort instruction pointer
};

// Async-signal-safe crash callback. MUST NOT allocate, lock a Mutex, throw, or call stdio.
// Use `os_signal_write_stderr` and stack buffers (`keel::format_u64_to` / `format_buf`).
using OsCrashCallback = void (*)(const OsCrashInfo& info, void* user_data) noexcept;

struct OsCrashOptions {
    bool print_default_report = true;
    bool print_backtrace = true;
    bool install_alt_stack = true;
    OsCrashCallback callback = nullptr;
    void* user_data = nullptr;
};

// Async-signal-safe direct write to stderr (`write(2, ...)` on POSIX, `WriteFile` on Win32).
void os_signal_write_stderr(StringRef msg) noexcept;

// Writes the default async-signal-safe crash banner (and optional backtrace) to stderr.
void os_crash_write_default_report(const OsCrashInfo& info, bool include_backtrace = true) noexcept;

// Installs fatal crash handlers (SIGSEGV/SIGBUS/SIGABRT/SIGFPE/SIGILL on POSIX with sigaltstack
// and SA_RESETHAND; SetUnhandledExceptionFilter + SIGABRT on Win32).
// Intended to be called from application `main()`, never automatically from libraries.
// After diagnostics, re-raises with OS default disposition so core dumps and signal exit codes are preserved.
Error os_crash_install_handler(const OsCrashOptions& opts = OsCrashOptions{}) noexcept;
void os_crash_uninstall_handler() noexcept;
[[nodiscard]] bool os_crash_handler_installed() noexcept;

} // namespace keel
