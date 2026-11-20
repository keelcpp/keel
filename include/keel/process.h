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
    bool capture_stdout = true;                   // Capture child stdout into ProcessOutput::stdout_text
    bool capture_stderr = true;                   // Capture child stderr into ProcessOutput::stderr_text
    bool merge_stderr = false;                    // Redirect child stderr into stdout_text
    usize max_output_bytes = 64u * 1024u * 1024u; // Cap per captured stream (0 = unlimited)
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

// Searches PATH (and PATHEXT on Win32) for an executable `program` without spawning a shell.
[[nodiscard]] Optional<String> process_which(StringRef program, Allocator* alloc = nullptr);

// Returns the current OS process identifier (PID).
[[nodiscard]] u64 process_id() noexcept;

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
