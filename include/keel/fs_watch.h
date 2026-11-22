#pragma once

#include "keel/core.h"
#include "keel/error.h"
#include "keel/function_ref.h"
#include "keel/memory.h"
#include "keel/result.h"
#include "keel/string.h"

namespace keel {

enum class WatchEventKind : u8 {
    Added = 0,
    Modified,
    Removed,
    Renamed
};

struct WatchEvent {
    WatchEventKind kind = WatchEventKind::Modified;
    StringRef path{}; // Valid for the duration of the poll() callback invocation
};

// Cross-platform move-only RAII filesystem directory watcher
// (`inotify` on Linux, `ReadDirectoryChangesW` on Windows, `kqueue` on macOS/BSD).
class FileWatcher {
public:
    FileWatcher() noexcept = default;
    ~FileWatcher() noexcept { close(); }

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    FileWatcher(FileWatcher&& other) noexcept : m_impl(other.m_impl), m_alloc(other.m_alloc) {
        other.m_impl = nullptr;
        other.m_alloc = nullptr;
    }

    FileWatcher& operator=(FileWatcher&& other) noexcept {
        if (this != &other) {
            close();
            m_impl = other.m_impl;
            m_alloc = other.m_alloc;
            other.m_impl = nullptr;
            other.m_alloc = nullptr;
        }
        return *this;
    }

    [[nodiscard]] static Result<FileWatcher> create(Allocator* alloc = nullptr);

    // Starts watching `dir_path` (and subdirectories if `recursive == true`).
    Error watch_dir(StringRef dir_path, bool recursive = false);

    // Stops watching `dir_path` (and its watched subdirectories if added recursively).
    Error unwatch_dir(StringRef dir_path) noexcept;

    // Polls for filesystem change events up to `timeout_ms` milliseconds (0 = non-blocking).
    // Invokes `callback` for each observed event; return false from `callback` to stop early.
    Error poll(u64 timeout_ms, FunctionRef<bool(const WatchEvent&)> callback);

    [[nodiscard]] bool is_open() const noexcept { return m_impl != nullptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

    void close() noexcept;

private:
    struct Impl;
    Impl* m_impl = nullptr;
    Allocator* m_alloc = nullptr;
};

} // namespace keel
