#pragma once

#include "keel/array.h"
#include "keel/function_ref.h"
#include "keel/result.h"
#include "keel/string.h"

namespace keel {

// Pure UTF-8 string path manipulation (paths always use `/` separator across all platforms).
[[nodiscard]] StringRef path_dirname(StringRef path) noexcept;
[[nodiscard]] StringRef path_basename(StringRef path) noexcept;
[[nodiscard]] StringRef path_stem(StringRef path) noexcept;
[[nodiscard]] StringRef path_extension(StringRef path) noexcept;
[[nodiscard]] bool path_is_absolute(StringRef path) noexcept;

void path_join_into(String& out, StringRef a, StringRef b);
[[nodiscard]] String path_join(StringRef a, StringRef b, Allocator* alloc = nullptr);

void path_normalize_into(String& out, StringRef path);
[[nodiscard]] String path_normalize(StringRef path, Allocator* alloc = nullptr);

struct FileStat {
    u64 size = 0;
    i64 mtime_ns = 0; // Modification time in nanoseconds since Unix epoch
    bool is_dir = false;
    bool is_file = false;
};

struct DirEntry {
    StringRef name{};
    StringRef full_path{};
    bool is_dir = false;
    bool is_file = false;
};

[[nodiscard]] bool fs_exists(StringRef path) noexcept;
[[nodiscard]] Result<FileStat> fs_stat(StringRef path) noexcept;

[[nodiscard]] Result<Array<u8>> fs_read_file(StringRef path, Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_read_text(StringRef path, Allocator* alloc = nullptr);
Error fs_write_file(StringRef path, ArrayRef<u8> data) noexcept;
Error fs_write_text(StringRef path, StringRef text) noexcept;

Error fs_create_dir(StringRef path) noexcept;
Error fs_create_dirs(StringRef path) noexcept;
Error fs_remove(StringRef path) noexcept;
Error fs_rename(StringRef from, StringRef to) noexcept;
Error fs_copy_file(StringRef from, StringRef to) noexcept;

[[nodiscard]] Result<String> fs_current_dir(Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_temp_dir(Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_exe_path(Allocator* alloc = nullptr);

// Iterates entries in `dir_path` (excluding `.` and `..`). Return false from `callback` to stop early.
Error fs_read_dir(StringRef dir_path, FunctionRef<bool(const DirEntry&)> callback);

} // namespace keel
