#pragma once

#include "keel/optional.h"
#include "keel/result.h"
#include "keel/string.h"

namespace keel {

enum class FileMode : u8 {
    Read = 0,
    Write,     // Truncate or create for writing
    Append,    // Create or open at end for writing
    ReadWrite  // Open existing or create for reading & writing
};

enum class SeekOrigin : u8 {
    Begin = 0,
    Current,
    End
};

enum class FileLockMode : u8 {
    Shared = 0, // Multiple concurrent readers
    Exclusive   // Single writer
};

// Move-only OS file handle with sticky error support for sequential writes.
class File {
public:
    static constexpr i64 kInvalidHandle = -1;

    File() noexcept = default;
    ~File() noexcept;

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;

    [[nodiscard]] static Result<File> open(StringRef path, FileMode mode = FileMode::Read) noexcept;

    [[nodiscard]] bool is_open() const noexcept { return m_handle != kInvalidHandle; }
    [[nodiscard]] explicit operator bool() const noexcept { return is_open() && m_sticky_error.is_ok(); }

    // Reads up to `buf.size()` bytes; returns actual bytes read (0 on EOF).
    [[nodiscard]] Result<usize> read(MutArrayRef<u8> buf) noexcept;

    // Positional read at `offset` bytes from start of file.
    // Note: POSIX leaves the seek cursor unchanged; Win32 synchronous handles update the file pointer.
    [[nodiscard]] Result<usize> pread(MutArrayRef<u8> buf, i64 offset) const noexcept;

    // Sequential write with sticky error: if an error occurred earlier, subsequent writes are no-ops.
    void write(ArrayRef<u8> data) noexcept;
    void write_str(StringRef str) noexcept {
        write(ArrayRef<u8>(reinterpret_cast<const u8*>(str.data()), str.size()));
    }

    // Positional write at `offset` bytes from start of file; returns bytes written.
    // Note: POSIX leaves the seek cursor unchanged; Win32 synchronous handles update the file pointer.
    [[nodiscard]] Result<usize> pwrite(ArrayRef<u8> data, i64 offset) noexcept;

    // Advisory whole-file lock (`flock` on POSIX, `LockFileEx` on Win32).
    // `try_lock` returns ErrorCode::WouldBlock if another holder conflicts.
    Error lock(FileLockMode mode = FileLockMode::Exclusive) noexcept;
    Error try_lock(FileLockMode mode = FileLockMode::Exclusive) noexcept;
    Error unlock() noexcept;

    Error flush() noexcept;
    [[nodiscard]] Result<i64> seek(i64 offset, SeekOrigin origin = SeekOrigin::Begin) noexcept;
    [[nodiscard]] Result<i64> tell() const noexcept;
    [[nodiscard]] Result<i64> size() const noexcept;

    [[nodiscard]] Error sticky_error() const noexcept { return m_sticky_error; }

    // Flushes and closes the file, returning any sticky write error or close error.
    Error close() noexcept;

private:
    friend File file_from_native_handle_internal(i64 handle) noexcept;
    explicit File(i64 handle) noexcept : m_handle(handle) {}

    i64 m_handle = kInvalidHandle;
    Error m_sticky_error{};
};

enum class StdStream : u8 {
    Stdin = 0,
    Stdout,
    Stderr
};

struct TerminalSize {
    u32 cols = 0;
    u32 rows = 0;
};

[[nodiscard]] Result<usize> stdin_read(MutArrayRef<u8> buf) noexcept;
void stdout_write(StringRef str) noexcept;
void stderr_write(StringRef str) noexcept;
void stdout_flush() noexcept;
void stderr_flush() noexcept;

// Returns true if the given standard stream is attached to an interactive terminal (TTY/console).
[[nodiscard]] bool is_tty(StdStream stream) noexcept;

// Returns current terminal dimensions in columns x rows, or kNullOpt if not a TTY or unavailable.
[[nodiscard]] Optional<TerminalSize> terminal_size(StdStream stream = StdStream::Stdout) noexcept;

// Enables ANSI/VT escape sequences and UTF-8 console output on Windows; returns true if ANSI output is active.
bool terminal_enable_ansi() noexcept;

} // namespace keel
