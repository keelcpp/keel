#pragma once

#include "keel/array.h"
#include "keel/function_ref.h"
#include "keel/result.h"
#include "keel/string.h"

namespace keel {

// Stack-first UTF-8 path buffer (255 inline bytes before spilling to Allocator*).
using PathBuf = SmallString<256>;

// Pure UTF-8 string path manipulation (paths always use `/` separator across all platforms).
[[nodiscard]] StringRef path_dirname(StringRef path) noexcept;
[[nodiscard]] StringRef path_basename(StringRef path) noexcept;
[[nodiscard]] StringRef path_stem(StringRef path) noexcept;
[[nodiscard]] StringRef path_extension(StringRef path) noexcept;
[[nodiscard]] bool path_is_absolute(StringRef path) noexcept;

void path_append(String& base, StringRef seg);

namespace detail {
template <usize N> void path_append_small(SmallString<N>& base, StringRef seg) {
    if (seg.empty()) {
        for (usize i = 0; i < base.size(); ++i) {
            if (base[i] == '\\') base[i] = '/';
        }
        return;
    }
    const char* cur = base.data();
    if (cur && seg.data() >= cur && seg.data() <= cur + base.size()) {
        const usize off = static_cast<usize>(seg.data() - cur);
        const usize len = seg.size();
        base.reserve(base.size() + 1 + len);
        seg = StringRef(base.data() + off, len);
    }
    if (base.empty() || path_is_absolute(seg)) {
        base.assign(seg);
    } else {
        base.reserve(base.size() + 1 + seg.size());
        const char last = base.back();
        if (last != '/' && last != '\\') {
            base.push_back('/');
        }
        usize seg_start = 0;
        while (seg_start < seg.size() && (seg[seg_start] == '/' || seg[seg_start] == '\\')) {
            ++seg_start;
        }
        base.append(seg.substr(seg_start));
    }
    for (usize i = 0; i < base.size(); ++i) {
        if (base[i] == '\\') base[i] = '/';
    }
}
} // namespace detail

template <usize N> void path_append(SmallString<N>& base, StringRef seg) {
    detail::path_append_small(base, seg);
}

void path_join_into(String& out, StringRef a, StringRef b);

template <usize N> void path_join_into(SmallString<N>& out, StringRef a, StringRef b) {
    if (b.empty()) {
        out.append(a);
    } else if (a.empty() || path_is_absolute(b)) {
        out.append(b);
    } else {
        out.reserve(out.size() + a.size() + 1 + b.size());
        out.append(a);
        const char last = a.back();
        if (last != '/' && last != '\\') {
            out.push_back('/');
        }
        usize b_start = 0;
        while (b_start < b.size() && (b[b_start] == '/' || b[b_start] == '\\')) {
            ++b_start;
        }
        out.append(b.substr(b_start));
    }
    for (usize i = 0; i < out.size(); ++i) {
        if (out[i] == '\\') out[i] = '/';
    }
}

[[nodiscard]] String path_join(StringRef a, StringRef b, Allocator* alloc = nullptr);
[[nodiscard]] String path_join(StringRef a, StringRef b, StringRef c, Allocator* alloc = nullptr);
[[nodiscard]] String path_join(StringRef a, StringRef b, StringRef c, StringRef d, Allocator* alloc = nullptr);
[[nodiscard]] String path_join_parts(ArrayRef<StringRef> parts, Allocator* alloc = nullptr);

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
[[nodiscard]] bool fs_is_dir(StringRef path) noexcept;
[[nodiscard]] bool fs_is_file(StringRef path) noexcept;
[[nodiscard]] Result<FileStat> fs_stat(StringRef path) noexcept;

[[nodiscard]] Result<Array<u8>> fs_read_file(StringRef path, Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_read_text(StringRef path, Allocator* alloc = nullptr);
Error fs_write_file(StringRef path, ArrayRef<u8> data) noexcept;
Error fs_write_text(StringRef path, StringRef text) noexcept;

Error fs_create_dir(StringRef path) noexcept;
Error fs_create_dirs(StringRef path) noexcept;
Error fs_remove(StringRef path) noexcept;
Error fs_remove_all(StringRef path) noexcept;
Error fs_rename(StringRef from, StringRef to) noexcept;
Error fs_copy_file(StringRef from, StringRef to) noexcept;

[[nodiscard]] Result<String> fs_current_dir(Allocator* alloc = nullptr);
Error fs_set_current_dir(StringRef path) noexcept;
[[nodiscard]] Result<String> fs_temp_dir(Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_home_dir(Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_config_dir(Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_data_dir(Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_cache_dir(Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_exe_path(Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_exe_dir(Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_absolute(StringRef path, Allocator* alloc = nullptr);
[[nodiscard]] Result<String> fs_realpath(StringRef path, Allocator* alloc = nullptr);

// Iterates immediate entries in `dir_path` (excluding `.` and `..`). Return false from `callback` to stop early.
Error fs_read_dir(StringRef dir_path, FunctionRef<bool(const DirEntry&)> callback);

// Recursively walks `dir_path` (without following directory symlinks/reparse points). Return false to stop early.
Error fs_walk_dir(StringRef dir_path, FunctionRef<bool(const DirEntry&)> callback);

} // namespace keel
