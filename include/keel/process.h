#pragma once

#include "keel/array.h"
#include "keel/error.h"
#include "keel/optional.h"
#include "keel/ref.h"
#include "keel/result.h"
#include "keel/string.h"

namespace keel {

struct ProcessEnvVar {
    StringRef name{};
    StringRef value{};
};

struct ProcessOptions {
    StringRef cwd{};                              // Working directory (empty = inherit parent cwd)
    ArrayRef<ProcessEnvVar> env{};                // Additional/overridden environment variables for child
    StringRef stdin_data{};                       // Optional data piped to child stdin before closing stdin
    bool capture_stdout = true;                   // Capture child stdout into ProcessOutput::stdout_text
    bool capture_stderr = true;                   // Capture child stderr into ProcessOutput::stderr_text
    bool merge_stderr = false;                    // Redirect child stderr into stdout_text
    usize max_output_bytes = 64u * 1024u * 1024u; // Cap per captured stream (0 = unlimited)
    u64 timeout_ms = 0;                           // Timeout in milliseconds (0 = unlimited; kills child on timeout)
};

struct ProcessOutput {
    i32 exit_code = -1;
    String stdout_text;
    String stderr_text;

    [[nodiscard]] bool ok() const noexcept { return exit_code == 0; }
};

// Synchronously runs a child process directly from `argv` (argv[0] = executable) without invoking a shell.
[[nodiscard]] Result<ProcessOutput> process_run(ArrayRef<StringRef> argv,
                                                const ProcessOptions& opts = {},
                                                Allocator* alloc = nullptr);

// Synchronously executes `command` through the platform shell (`/bin/sh -c` on POSIX, `cmd.exe /c` on Win32).
[[nodiscard]] Result<ProcessOutput> process_run_shell(StringRef command,
                                                      const ProcessOptions& opts = {},
                                                      Allocator* alloc = nullptr);

// Move-only asynchronous child process handle (`spawn`, `try_wait`, `wait`, `wait_for_ms`, `kill`).
class ChildProcess {
public:
    ChildProcess() noexcept = default;
    ~ChildProcess() noexcept;

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    ChildProcess(ChildProcess&& other) noexcept
        : m_handle(other.m_handle), m_pid(other.m_pid), m_exit_code(other.m_exit_code), m_exited(other.m_exited) {
        other.m_handle = 0;
        other.m_pid = 0;
        other.m_exit_code = -1;
        other.m_exited = false;
    }

    ChildProcess& operator=(ChildProcess&& other) noexcept {
        if (this != &other) {
            close_handle();
            m_handle = other.m_handle;
            m_pid = other.m_pid;
            m_exit_code = other.m_exit_code;
            m_exited = other.m_exited;
            other.m_handle = 0;
            other.m_pid = 0;
            other.m_exit_code = -1;
            other.m_exited = false;
        }
        return *this;
    }

    // Spawns a child process asynchronously from `argv` (respects `opts.cwd` and `opts.env`).
    [[nodiscard]] static Result<ChildProcess> spawn(ArrayRef<StringRef> argv,
                                                    const ProcessOptions& opts = {});

    [[nodiscard]] bool valid() const noexcept { return m_handle != 0 || m_exited; }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }
    [[nodiscard]] u64 pid() const noexcept { return m_pid; }

    // Non-blocking poll: returns exit code if exited, kNullOpt if still running.
    [[nodiscard]] Result<Optional<i32>> try_wait() noexcept;

    // Blocks until the child exits and returns its exit code.
    [[nodiscard]] Result<i32> wait() noexcept;

    // Blocks up to `timeout_ms` ms; returns ErrorCode::TimedOut if still running.
    [[nodiscard]] Result<i32> wait_for_ms(u64 timeout_ms) noexcept;

    // Forcefully terminates the child process (`SIGKILL` on POSIX, `TerminateProcess` on Win32).
    Error kill() noexcept;

private:
    void close_handle() noexcept;

    usize m_handle = 0;
    u64 m_pid = 0;
    i32 m_exit_code = -1;
    bool m_exited = false;
};

// Returns the current process's command-line arguments as UTF-8 strings (even on Windows).
[[nodiscard]] Result<Array<String>> process_args(Allocator* alloc = nullptr);

// Installs an async-signal-safe graceful-shutdown handler for SIGINT/SIGTERM (or Ctrl+C/Break on Win32).
void process_install_interrupt_handler() noexcept;
[[nodiscard]] bool process_interrupt_requested() noexcept;
void process_reset_interrupt() noexcept;

// Searches PATH (and PATHEXT on Win32) for an executable `program` without spawning a shell.
[[nodiscard]] Optional<String> process_which(StringRef program, Allocator* alloc = nullptr);

// Platform default executable filename extension (".exe" on Win32, "" elsewhere).
[[nodiscard]] constexpr StringRef process_exe_extension() noexcept {
#if defined(_WIN32)
    return ".exe";
#else
    return "";
#endif
}

// Returns the current OS process identifier (PID).
[[nodiscard]] u64 process_id() noexcept;

// Returns remaining stack space in bytes for the calling thread, or kNullOpt if unavailable on the platform.
[[nodiscard]] Optional<usize> thread_stack_remaining_bytes() noexcept;

// Process memory usage snapshot in bytes (0 for fields unavailable on the platform).
struct ProcessMemoryInfo {
    u64 rss_bytes{0};      // Total resident physical RAM (WorkingSet / VmRSS / resident_size)
    u64 private_bytes{0};  // Private resident RAM (Win Task Manager / macOS Activity Monitor / Linux RES-SHR)
    u64 commit_bytes{0};   // Committed private virtual memory (Win PrivateUsage / Linux data+stack / macOS internal)
};

// Returns a snapshot of the current process's memory usage in bytes.
[[nodiscard]] ProcessMemoryInfo process_memory_info() noexcept;

// Returns the current process resident set size (physical RAM in bytes), or 0 if unavailable.
[[nodiscard]] u64 process_rss_bytes() noexcept;

// Returns cumulative user + kernel CPU time consumed by the current process in nanoseconds, or 0 if unavailable.
[[nodiscard]] u64 process_cpu_time_ns() noexcept;

enum class LockScope : u8 {
    User = 0, // Per-user / per-session single-instance scope (default)
    System    // Machine-wide single-instance scope
};

// Cross-platform exclusive single-instance process lock (RAII, move-only).
// Automatically released by the OS kernel even on process crashes or SIGKILL.
class ProcessLock {
public:
    ProcessLock() noexcept = default;
    ~ProcessLock() noexcept { release(); }

    ProcessLock(ProcessLock&& other) noexcept : m_handle(other.m_handle) { other.m_handle = 0; }
    ProcessLock& operator=(ProcessLock&& other) noexcept {
        if (this != &other) {
            release();
            m_handle = other.m_handle;
            other.m_handle = 0;
        }
        return *this;
    }

    ProcessLock(const ProcessLock&) = delete;
    ProcessLock& operator=(const ProcessLock&) = delete;

    // Non-blocking acquisition. Returns ErrorCode::AlreadyExists if another instance holds `app_id`.
    [[nodiscard]] static Result<ProcessLock> try_acquire(StringRef app_id,
                                                         LockScope scope = LockScope::User) noexcept;

    [[nodiscard]] bool is_locked() const noexcept { return m_handle != 0; }
    explicit operator bool() const noexcept { return is_locked(); }

    void release() noexcept;

private:
    explicit ProcessLock(usize handle) noexcept : m_handle(handle) {}
    usize m_handle = 0;
};

} // namespace keel
