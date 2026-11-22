#pragma once

#include "keel/core.h"
#include "keel/error.h"
#include "keel/ref.h"
#include "keel/result.h"
#include "keel/string.h"

namespace keel {

enum class MapMode : u8 {
    ReadOnly = 0, // Shared read-only memory mapping
    ReadWrite,    // Shared read-write memory mapping (mutations write back to the underlying file)
    CopyOnWrite   // Private copy-on-write mapping (mutations stay private in memory, file on disk unchanged)
};

// Move-only RAII memory-mapped file view (`mmap` on POSIX, `CreateFileMappingW` + `MapViewOfFile` on Win32).
// Gracefully supports empty (0-byte) files (`is_open() == true`, `data() == nullptr`, `size() == 0`).
class MappedFile {
public:
    MappedFile() noexcept = default;
    ~MappedFile() noexcept { close(); }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept
        : m_data(other.m_data),
          m_size(other.m_size),
          m_file_handle(other.m_file_handle),
          m_map_handle(other.m_map_handle),
          m_mode(other.m_mode),
          m_open(other.m_open) {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_file_handle = nullptr;
        other.m_map_handle = nullptr;
        other.m_open = false;
    }

    MappedFile& operator=(MappedFile&& other) noexcept {
        if (this != &other) {
            close();
            m_data = other.m_data;
            m_size = other.m_size;
            m_file_handle = other.m_file_handle;
            m_map_handle = other.m_map_handle;
            m_mode = other.m_mode;
            m_open = other.m_open;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_file_handle = nullptr;
            other.m_map_handle = nullptr;
            other.m_open = false;
        }
        return *this;
    }

    // Maps an existing file at `path` in `ReadOnly`, `ReadWrite`, or `CopyOnWrite` mode.
    [[nodiscard]] static Result<MappedFile> open(StringRef path, MapMode mode = MapMode::ReadOnly) noexcept;

    // Creates or truncates `path` to exactly `size` bytes and maps it in `ReadWrite` mode.
    [[nodiscard]] static Result<MappedFile> open_create(StringRef path, usize size) noexcept;

    [[nodiscard]] bool is_open() const noexcept { return m_open; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_open; }
    [[nodiscard]] MapMode mode() const noexcept { return m_mode; }

    [[nodiscard]] const u8* data() const noexcept { return m_data; }
    [[nodiscard]] u8* mutable_data() noexcept { return (m_mode != MapMode::ReadOnly) ? m_data : nullptr; }
    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] ArrayRef<u8> bytes() const noexcept { return ArrayRef<u8>(m_data, m_size); }
    [[nodiscard]] MutArrayRef<u8> mutable_bytes() noexcept {
        return (m_mode != MapMode::ReadOnly) ? MutArrayRef<u8>(m_data, m_size) : MutArrayRef<u8>{};
    }
    [[nodiscard]] StringRef text() const noexcept {
        return StringRef(reinterpret_cast<const char*>(m_data), m_size);
    }

    // Flushes modified pages to disk (`msync(MS_SYNC)` on POSIX, `FlushViewOfFile` + `FlushFileBuffers` on Win32).
    // No-op returning Ok on ReadOnly, CopyOnWrite, or empty mappings.
    Error flush() noexcept;

    // Unmaps memory and releases underlying OS handles.
    void close() noexcept;

private:
    u8* m_data = nullptr;
    usize m_size = 0;
    void* m_file_handle = nullptr;
    void* m_map_handle = nullptr;
    MapMode m_mode = MapMode::ReadOnly;
    bool m_open = false;
};

} // namespace keel
