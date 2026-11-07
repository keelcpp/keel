#pragma once

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

    // Sequential write with sticky error: if an error occurred earlier, subsequent writes are no-ops.
    void write(ArrayRef<u8> data) noexcept;
    void write_str(StringRef str) noexcept {
        write(ArrayRef<u8>(reinterpret_cast<const u8*>(str.data()), str.size()));
    }

    Error flush() noexcept;
    [[nodiscard]] Result<i64> seek(i64 offset, SeekOrigin origin = SeekOrigin::Begin) noexcept;
    [[nodiscard]] Result<i64> tell() const noexcept;
    [[nodiscard]] Result<i64> size() const noexcept;

    [[nodiscard]] Error sticky_error() const noexcept { return m_sticky_error; }

    // Flushes and closes the file, returning any sticky write error or close error.
    Error close() noexcept;

private:
    explicit File(i64 handle) noexcept : m_handle(handle) {}

    i64 m_handle = kInvalidHandle;
    Error m_sticky_error{};
};

void stdout_write(StringRef str) noexcept;
void stderr_write(StringRef str) noexcept;
void stdout_flush() noexcept;
void stderr_flush() noexcept;

} // namespace keel
