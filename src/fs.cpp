#include "keel/fs.h"
#include "keel/arena.h"
#include "keel/env.h"
#include "keel/io.h"
#include "keel/number.h"
#include "keel/process.h"
#include "keel/small_array.h"
#include "keel/time.h"

#include <atomic>

#if defined(_WIN32)
#include "win32_compat.h"
#else
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#if !defined(__EMSCRIPTEN__)
#include <pwd.h>
#endif
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace keel {

namespace detail {
#if defined(_WIN32)
Error win32_error_to_keel(DWORD err) noexcept;
bool utf8_path_to_win32_wide(StringRef utf8_path, SmallArray<wchar_t, 260>& out);
void win32_wide_to_utf8_path(const wchar_t* wstr, usize wlen, String& out);
#else
Error errno_to_keel(int err) noexcept;
#endif

constexpr bool is_sep(char c) noexcept { return c == '/' || c == '\\'; }

constexpr usize strip_trailing_seps(StringRef path) noexcept {
    usize n = path.size();
    while (n > 1 && is_sep(path[n - 1])) {
        // Keep root after Windows drive colon `C:/`
        if (n == 3 && path[1] == ':') break;
        --n;
    }
    return n;
}
} // namespace detail

StringRef path_dirname(StringRef path) noexcept {
    if (path.empty()) return ".";
    const usize len = detail::strip_trailing_seps(path);
    const StringRef trimmed = path.substr(0, len);

    usize last_sep = kInvalidIndex;
    for (usize i = trimmed.size(); i > 0; --i) {
        if (detail::is_sep(trimmed[i - 1])) {
            last_sep = i - 1;
            break;
        }
    }

    if (last_sep == kInvalidIndex) {
        return ".";
    }
    if (last_sep == 0) {
        return trimmed.substr(0, 1);
    }
    if (last_sep == 2 && trimmed[1] == ':') {
        return trimmed.substr(0, 3);
    }
    while (last_sep > 1 && detail::is_sep(trimmed[last_sep - 1])) {
        --last_sep;
    }
    return trimmed.substr(0, last_sep);
}

StringRef path_basename(StringRef path) noexcept {
    if (path.empty()) return "";
    usize end = path.size();
    while (end > 0 && detail::is_sep(path[end - 1])) {
        --end;
    }
    if (end == 0) return path.substr(0, 1);

    usize start = end;
    while (start > 0 && !detail::is_sep(path[start - 1])) {
        --start;
    }
    return path.substr(start, end - start);
}

StringRef path_extension(StringRef path) noexcept {
    const StringRef base = path_basename(path);
    if (base.empty() || base == "." || base == "..") return "";
    const usize dot = base.rfind('.');
    if (dot == kInvalidIndex || dot == 0) return ""; // Leading dot `.gitignore` has no extension
    return base.substr(dot);
}

StringRef path_stem(StringRef path) noexcept {
    const StringRef base = path_basename(path);
    const StringRef ext = path_extension(base);
    if (ext.empty()) return base;
    return base.substr(0, base.size() - ext.size());
}

bool path_is_absolute(StringRef path) noexcept {
    if (path.empty()) return false;
    if (detail::is_sep(path[0])) return true;
    if (path.size() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && detail::is_sep(path[2])) {
        return true;
    }
    return false;
}

void path_append(String& base, StringRef seg) {
    if (seg.empty()) {
        for (usize i = 0; i < base.size(); ++i) {
            if (base[i] == '\\') base[i] = '/';
        }
        return;
    }
    const char* cur = base.data();
    if (cur && seg.data() >= cur && seg.data() <= cur + base.size()) {
        const usize off = as<usize>(seg.data() - cur);
        const usize len = seg.size();
        base.reserve(base.size() + 1 + len);
        seg = StringRef(base.data() + off, len);
    }
    if (base.empty() || path_is_absolute(seg)) {
        base.assign(seg);
    } else {
        base.reserve(base.size() + 1 + seg.size());
        if (!detail::is_sep(base.back())) {
            base.push_back('/');
        }
        usize seg_start = 0;
        while (seg_start < seg.size() && detail::is_sep(seg[seg_start])) {
            ++seg_start;
        }
        base.append(seg.substr(seg_start));
    }
    for (usize i = 0; i < base.size(); ++i) {
        if (base[i] == '\\') base[i] = '/';
    }
}

void path_join_into(String& out, StringRef a, StringRef b) {
    if (b.empty()) {
        out.append(a);
    } else if (a.empty() || path_is_absolute(b)) {
        out.append(b);
    } else {
        out.reserve(out.size() + a.size() + 1 + b.size());
        out.append(a);
        if (!detail::is_sep(a.back())) {
            out.push_back('/');
        }
        usize b_start = 0;
        while (b_start < b.size() && detail::is_sep(b[b_start])) {
            ++b_start;
        }
        out.append(b.substr(b_start));
    }
    for (usize i = 0; i < out.size(); ++i) {
        if (out[i] == '\\') out[i] = '/';
    }
}

String path_join(StringRef a, StringRef b, Allocator* alloc) {
    String out(alloc);
    path_join_into(out, a, b);
    return out;
}

String path_join(StringRef a, StringRef b, StringRef c, Allocator* alloc) {
    const StringRef parts[3] = {a, b, c};
    return path_join_parts(ArrayRef<StringRef>(parts, 3), alloc);
}

String path_join(StringRef a, StringRef b, StringRef c, StringRef d, Allocator* alloc) {
    const StringRef parts[4] = {a, b, c, d};
    return path_join_parts(ArrayRef<StringRef>(parts, 4), alloc);
}

String path_join_parts(ArrayRef<StringRef> parts, Allocator* alloc) {
    String out(alloc);
    usize total_hint = 0;
    for (usize i = 0; i < parts.size(); ++i) {
        total_hint += parts[i].size() + 1;
    }
    if (total_hint > 0) out.reserve(total_hint);
    for (usize i = 0; i < parts.size(); ++i) {
        path_append(out, parts[i]);
    }
    return out;
}

void path_normalize_into(String& out, StringRef path) {
    if (path.empty()) {
        out.append(".");
        return;
    }

    ArenaTemp scratch = get_scratch(out.allocator());
    SmallArray<StringRef, 32> parts(scratch.allocator());

    StringRef prefix{};
    usize i = 0;
    bool rooted = false;

    if (path.size() >= 2 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') {
        prefix = path.substr(0, 2);
        i = 2;
        if (i < path.size() && detail::is_sep(path[i])) {
            rooted = true;
            ++i;
        }
    } else if (detail::is_sep(path[0])) {
        rooted = true;
        i = 1;
    }

    while (i < path.size()) {
        while (i < path.size() && detail::is_sep(path[i])) ++i;
        if (i >= path.size()) break;
        const usize start = i;
        while (i < path.size() && !detail::is_sep(path[i])) ++i;
        const StringRef seg = path.substr(start, i - start);
        if (seg == ".") {
            continue;
        }
        if (seg == "..") {
            if (!parts.empty() && parts[parts.size() - 1] != "..") {
                parts.pop_back();
            } else if (!rooted) {
                parts.push_back(seg);
            }
        } else {
            parts.push_back(seg);
        }
    }

    if (!prefix.empty()) {
        out.append(prefix);
    }
    if (rooted) {
        out.push_back('/');
    }
    for (usize k = 0; k < parts.size(); ++k) {
        if (k > 0) out.push_back('/');
        out.append(parts[k]);
    }
    if (out.empty()) {
        out.append(".");
    }
}

String path_normalize(StringRef path, Allocator* alloc) {
    String out(alloc);
    path_normalize_into(out, path);
    return out;
}

bool fs_exists(StringRef path) noexcept {
    if (path.empty()) return false;
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) return false;
    return GetFileAttributesW(wpath.data()) != INVALID_FILE_ATTRIBUTES;
#else
    ArenaTemp scratch = get_scratch();
    String cpath = String::from(path, scratch.allocator());
    struct stat st {};
    return ::stat(cpath.c_str(), &st) == 0;
#endif
}

bool fs_is_dir(StringRef path) noexcept {
    auto st = fs_stat(path);
    return st.is_ok() && st->is_dir;
}

bool fs_is_file(StringRef path) noexcept {
    auto st = fs_stat(path);
    return st.is_ok() && st->is_file;
}

Result<FileStat> fs_stat(StringRef path) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) return Error{ErrorCode::InvalidArgument, 0};

    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(wpath.data(), GetFileExInfoStandard, &data)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    FileStat st{};
    st.size = (as<u64>(data.nFileSizeHigh) << 32) | as<u64>(data.nFileSizeLow);
    const i64 ticks = (as<i64>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                      as<i64>(data.ftLastWriteTime.dwLowDateTime);
    // Windows FILETIME is 100-ns intervals since 1601-01-01; Unix epoch is 116444736000000000 ticks later
    st.mtime_ns = (ticks - 116444736000000000LL) * 100LL;
    st.is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    st.is_file = !st.is_dir;
    return st;
#else
    ArenaTemp scratch = get_scratch();
    String cpath = String::from(path, scratch.allocator());
    struct stat st {};
    if (::stat(cpath.c_str(), &st) != 0) {
        return detail::errno_to_keel(errno);
    }
    FileStat out{};
    out.size = as<u64>(st.st_size);
#if defined(__APPLE__)
    out.mtime_ns = as<i64>(st.st_mtimespec.tv_sec) * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
    out.mtime_ns = as<i64>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
#endif
    out.is_dir = S_ISDIR(st.st_mode);
    out.is_file = S_ISREG(st.st_mode);
    return out;
#endif
}

Result<Array<u8>> fs_read_file(StringRef path, Allocator* alloc) {
    KEEL_TRY_ASSIGN(File f, File::open(path, FileMode::Read));
    KEEL_TRY_ASSIGN(i64 sz, f.size());
    if (sz < 0) return Error{ErrorCode::IoError, 0};

    Array<u8> buf(alloc);
    buf.resize(as<usize>(sz));
    usize total = 0;
    while (total < buf.size()) {
        KEEL_TRY_ASSIGN(usize n, f.read(MutArrayRef<u8>(buf.data() + total, buf.size() - total)));
        if (n == 0) break;
        total += n;
    }
    buf.resize(total);
    return buf;
}

Result<String> fs_read_text(StringRef path, Allocator* alloc) {
    KEEL_TRY_ASSIGN(File f, File::open(path, FileMode::Read));
    KEEL_TRY_ASSIGN(i64 sz, f.size());
    if (sz < 0) return Error{ErrorCode::IoError, 0};

    String text(alloc);
    text.resize(as<usize>(sz));
    usize total = 0;
    while (total < text.size()) {
        KEEL_TRY_ASSIGN(
            usize n, f.read(MutArrayRef<u8>(reinterpret_cast<u8*>(text.data() + total), text.size() - total)));
        if (n == 0) break;
        total += n;
    }
    text.resize(total);
    return text;
}

Error fs_write_file(StringRef path, ArrayRef<u8> data) noexcept {
    KEEL_TRY_ASSIGN(File f, File::open(path, FileMode::Write));
    f.write(data);
    return f.close();
}

Error fs_write_text(StringRef path, StringRef text) noexcept {
    return fs_write_file(path, ArrayRef<u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
}

Error fs_write_file_atomic(StringRef path, ArrayRef<u8> data) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};

    static std::atomic<u64> s_atomic_seq{0};
    const u64 pid = process_id();
    const StringRef dir = path_dirname(path);
    const StringRef base = path_basename(path);
    if (base.empty()) return Error{ErrorCode::InvalidArgument, 0};

    ArenaTemp scratch = get_scratch();

    auto build_tmp_path = [&](u64 seq) -> String {
        String name(scratch.allocator());
        name.reserve(base.size() + 48u);
        name.push_back('.');
        name.append(base);
        name.append(".keel_tmp_");
        char num_buf[32]{};
        usize n1 = format_u64(num_buf, sizeof(num_buf), pid);
        name.append(StringRef(num_buf, n1));
        name.push_back('_');
        usize n2 = format_u64(num_buf, sizeof(num_buf), seq);
        name.append(StringRef(num_buf, n2));
        if (dir == "." && path.find('/') == kInvalidIndex && path.find('\\') == kInvalidIndex) {
            return name;
        }
        return path_join(dir, name, scratch.allocator());
    };

#if defined(_WIN32)
    SmallArray<wchar_t, 260> wtarget(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wtarget)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }

    HANDLE h = INVALID_HANDLE_VALUE;
    SmallArray<wchar_t, 260> wtmp(scratch.allocator());
    for (u32 attempt = 0; attempt < 64u; ++attempt) {
        const u64 seq = s_atomic_seq.fetch_add(1u, std::memory_order_relaxed);
        const String tmp_path = build_tmp_path(seq);
        wtmp.clear();
        if (!detail::utf8_path_to_win32_wide(tmp_path, wtmp)) {
            return Error{ErrorCode::InvalidArgument, 0};
        }
        h = CreateFileW(
            wtmp.data(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (h != INVALID_HANDLE_VALUE) {
            break;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
            return detail::win32_error_to_keel(err);
        }
    }
    if (h == INVALID_HANDLE_VALUE) {
        return Error{ErrorCode::AlreadyExists, 0};
    }

    Error write_err = Error::ok();
    usize written_total = 0;
    while (written_total < data.size()) {
        const usize rem = data.size() - written_total;
        const DWORD chunk = (rem > 0x40000000u) ? 0x40000000u : as<DWORD>(rem);
        DWORD written = 0;
        if (!WriteFile(h, data.data() + written_total, chunk, &written, nullptr)) {
            write_err = detail::win32_error_to_keel(GetLastError());
            break;
        }
        if (written == 0) {
            write_err = Error{ErrorCode::IoError, 0};
            break;
        }
        written_total += as<usize>(written);
    }

    if (write_err.is_ok() && !FlushFileBuffers(h)) {
        write_err = detail::win32_error_to_keel(GetLastError());
    }
    if (!CloseHandle(h) && write_err.is_ok()) {
        write_err = detail::win32_error_to_keel(GetLastError());
    }
    if (write_err.is_err()) {
        DeleteFileW(wtmp.data());
        return write_err;
    }

    if (!MoveFileExW(wtmp.data(), wtarget.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD move_win_err = GetLastError();
        if (move_win_err == ERROR_ACCESS_DENIED) {
            const DWORD attrs = GetFileAttributesW(wtarget.data());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
                SetFileAttributesW(wtarget.data(), attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
                if (MoveFileExW(wtmp.data(), wtarget.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    return Error::ok();
                }
                move_win_err = GetLastError();
            }
        }
        const Error move_err = detail::win32_error_to_keel(move_win_err);
        DeleteFileW(wtmp.data());
        return move_err;
    }
    return Error::ok();
#else
    const String ctarget = String::from(path, scratch.allocator());
    struct stat existing_st{};
    const bool had_existing = (::stat(ctarget.c_str(), &existing_st) == 0);

    int fd = -1;
    String ctmp(scratch.allocator());
    for (u32 attempt = 0; attempt < 64u; ++attempt) {
        const u64 seq = s_atomic_seq.fetch_add(1u, std::memory_order_relaxed);
        ctmp = build_tmp_path(seq);
        const mode_t create_mode = had_existing ? (existing_st.st_mode & 0777u) : static_cast<mode_t>(0666);
        int open_flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_CLOEXEC)
        open_flags |= O_CLOEXEC;
#endif
        fd = ::open(ctmp.c_str(), open_flags, create_mode);
        if (fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            return detail::errno_to_keel(errno);
        }
    }
    if (fd < 0) {
        return Error{ErrorCode::AlreadyExists, EEXIST};
    }

    Error write_err = Error::ok();
    usize written_total = 0;
    while (written_total < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written_total, data.size() - written_total);
        if (n < 0) {
            if (errno == EINTR) continue;
            write_err = detail::errno_to_keel(errno);
            break;
        }
        if (n == 0) {
            write_err = Error{ErrorCode::IoError, 0};
            break;
        }
        written_total += as<usize>(n);
    }

    if (write_err.is_ok()) {
#if !defined(__EMSCRIPTEN__)
        if (::fsync(fd) != 0 && errno != EINVAL && errno != EROFS && errno != ENOTSUP) {
            write_err = detail::errno_to_keel(errno);
        }
#endif
    }
    if (had_existing && write_err.is_ok()) {
        (void)::fchmod(fd, existing_st.st_mode & 0777u);
    }
    if (::close(fd) != 0 && write_err.is_ok()) {
        write_err = detail::errno_to_keel(errno);
    }
    if (write_err.is_err()) {
        (void)::unlink(ctmp.c_str());
        return write_err;
    }

    if (std::rename(ctmp.c_str(), ctarget.c_str()) != 0) {
        const Error ren_err = detail::errno_to_keel(errno);
        (void)::unlink(ctmp.c_str());
        return ren_err;
    }
#if !defined(__EMSCRIPTEN__)
    // Best-effort parent directory fsync so the directory entry rename survives power loss.
    {
        const String cdir = String::from(dir.empty() ? StringRef(".") : dir, scratch.allocator());
        int dir_flags = O_RDONLY;
#if defined(O_DIRECTORY)
        dir_flags |= O_DIRECTORY;
#endif
#if defined(O_CLOEXEC)
        dir_flags |= O_CLOEXEC;
#endif
        const int dir_fd = ::open(cdir.c_str(), dir_flags);
        if (dir_fd >= 0) {
            (void)::fsync(dir_fd);
            (void)::close(dir_fd);
        }
    }
#endif
    return Error::ok();
#endif
}

Error fs_write_text_atomic(StringRef path, StringRef text) noexcept {
    return fs_write_file_atomic(path, ArrayRef<u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
}

Error fs_create_dir(StringRef path) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) return Error{ErrorCode::InvalidArgument, 0};
    if (!CreateDirectoryW(wpath.data(), nullptr)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return Error::ok();
#else
    ArenaTemp scratch = get_scratch();
    String cpath = String::from(path, scratch.allocator());
    if (::mkdir(cpath.c_str(), 0777) != 0) {
        return detail::errno_to_keel(errno);
    }
    return Error::ok();
#endif
}

Error fs_create_dirs(StringRef path) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
    ArenaTemp scratch = get_scratch();
    String norm = path_normalize(path, scratch.allocator());
    if (fs_exists(norm)) return Error::ok();

    for (usize i = 1; i <= norm.size(); ++i) {
        if (i == norm.size() || norm[i] == '/') {
            const StringRef sub = norm.substr(0, i);
            if (sub.empty() || (sub.size() == 2 && sub[1] == ':')) continue;
            if (!fs_exists(sub)) {
                const Error err = fs_create_dir(sub);
                if (err.is_err() && err.code != ErrorCode::AlreadyExists) {
                    return err;
                }
            }
        }
    }
    return Error::ok();
}

Error fs_remove(StringRef path) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) return Error{ErrorCode::InvalidArgument, 0};
    const DWORD attrs = GetFileAttributesW(wpath.data());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return detail::win32_error_to_keel(GetLastError());
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if (!RemoveDirectoryW(wpath.data())) {
            return detail::win32_error_to_keel(GetLastError());
        }
    } else {
        if (!DeleteFileW(wpath.data())) {
            return detail::win32_error_to_keel(GetLastError());
        }
    }
    return Error::ok();
#else
    ArenaTemp scratch = get_scratch();
    String cpath = String::from(path, scratch.allocator());
    if (std::remove(cpath.c_str()) != 0) {
        return detail::errno_to_keel(errno);
    }
    return Error::ok();
#endif
}

Error fs_remove_all(StringRef path) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    SmallArray<wchar_t, 260> wpath;
    if (!detail::utf8_path_to_win32_wide(path, wpath)) return Error{ErrorCode::InvalidArgument, 0};
    const DWORD attrs = GetFileAttributesW(wpath.data());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return Error::ok();
        return detail::win32_error_to_keel(e);
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
            PathBuf pattern;
            path_join_into(pattern, path, "*");
            SmallArray<wchar_t, 260> wpattern;
            if (!detail::utf8_path_to_win32_wide(pattern, wpattern)) {
                return Error{ErrorCode::InvalidArgument, 0};
            }
            WIN32_FIND_DATAW fdata{};
            HANDLE hFind = FindFirstFileW(wpattern.data(), &fdata);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (fdata.cFileName[0] == L'.' &&
                        (fdata.cFileName[1] == L'\0' ||
                         (fdata.cFileName[1] == L'.' && fdata.cFileName[2] == L'\0'))) {
                        continue;
                    }
                    String name_str;
                    usize wlen = 0;
                    while (fdata.cFileName[wlen] != L'\0') ++wlen;
                    detail::win32_wide_to_utf8_path(fdata.cFileName, wlen, name_str);
                    PathBuf child;
                    path_join_into(child, path, name_str);
                    const Error child_err = fs_remove_all(child);
                    if (child_err.is_err()) {
                        FindClose(hFind);
                        return child_err;
                    }
                } while (FindNextFileW(hFind, &fdata));
                FindClose(hFind);
            } else {
                const DWORD find_err = GetLastError();
                if (find_err != ERROR_FILE_NOT_FOUND && find_err != ERROR_PATH_NOT_FOUND) {
                    return detail::win32_error_to_keel(find_err);
                }
            }
        }
        if ((attrs & FILE_ATTRIBUTE_READONLY) != 0) {
            SetFileAttributesW(wpath.data(), attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
        }
        if (!RemoveDirectoryW(wpath.data())) {
            const DWORD e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) {
                return detail::win32_error_to_keel(e);
            }
        }
        return Error::ok();
    }
    if ((attrs & FILE_ATTRIBUTE_READONLY) != 0) {
        SetFileAttributesW(wpath.data(), attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
    }
    if (!DeleteFileW(wpath.data())) {
        const DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) {
            return detail::win32_error_to_keel(e);
        }
    }
    return Error::ok();
#else
    PathBuf cpath(path);
    struct stat st {};
    if (::lstat(cpath.c_str(), &st) != 0) {
        return errno == ENOENT ? Error::ok() : detail::errno_to_keel(errno);
    }
    if (!S_ISDIR(st.st_mode)) {
        if (::unlink(cpath.c_str()) != 0 && errno != ENOENT) {
            return detail::errno_to_keel(errno);
        }
        return Error::ok();
    }
    DIR* dir = ::opendir(cpath.c_str());
    if (!dir) {
        return errno == ENOENT ? Error::ok() : detail::errno_to_keel(errno);
    }
    while (struct dirent* ent = ::readdir(dir)) {
        const StringRef name(ent->d_name);
        if (name == "." || name == "..") continue;
        PathBuf child;
        path_join_into(child, path, name);
        const Error err = fs_remove_all(child);
        if (err.is_err()) {
            ::closedir(dir);
            return err;
        }
    }
    ::closedir(dir);
    if (::rmdir(cpath.c_str()) != 0 && errno != ENOENT) {
        return detail::errno_to_keel(errno);
    }
    return Error::ok();
#endif
}

Error fs_rename(StringRef from, StringRef to) noexcept {
    if (from.empty() || to.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wfrom(scratch.allocator());
    SmallArray<wchar_t, 260> wto(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(from, wfrom) || !detail::utf8_path_to_win32_wide(to, wto)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    if (!MoveFileExW(wfrom.data(), wto.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return Error::ok();
#else
    ArenaTemp scratch = get_scratch();
    String cfrom = String::from(from, scratch.allocator());
    String cto = String::from(to, scratch.allocator());
    if (std::rename(cfrom.c_str(), cto.c_str()) != 0) {
        return detail::errno_to_keel(errno);
    }
    return Error::ok();
#endif
}

Error fs_copy_file(StringRef from, StringRef to) noexcept {
    if (from.empty() || to.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wfrom(scratch.allocator());
    SmallArray<wchar_t, 260> wto(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(from, wfrom) || !detail::utf8_path_to_win32_wide(to, wto)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    if (!CopyFileW(wfrom.data(), wto.data(), FALSE)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return Error::ok();
#else
    KEEL_TRY_ASSIGN(File src, File::open(from, FileMode::Read));
    KEEL_TRY_ASSIGN(File dst, File::open(to, FileMode::Write));
    u8 buf[16384];
    for (;;) {
        KEEL_TRY_ASSIGN(usize n, src.read(MutArrayRef<u8>(buf, sizeof(buf))));
        if (n == 0) break;
        dst.write(ArrayRef<u8>(buf, n));
    }
    return dst.close();
#endif
}

Result<String> fs_current_dir(Allocator* alloc) {
#if defined(_WIN32)
    const DWORD needed = GetCurrentDirectoryW(0, nullptr);
    if (needed == 0) return detail::win32_error_to_keel(GetLastError());
    ArenaTemp scratch = get_scratch(alloc);
    SmallArray<wchar_t, 260> wbuf(scratch.allocator());
    wbuf.reserve(as<usize>(needed) + 1);
    const DWORD len = GetCurrentDirectoryW(needed, wbuf.data());
    if (len == 0) return detail::win32_error_to_keel(GetLastError());
    String out(alloc);
    detail::win32_wide_to_utf8_path(wbuf.data(), as<usize>(len), out);
    return out;
#else
    char buf[4096];
    if (!::getcwd(buf, sizeof(buf))) {
        return detail::errno_to_keel(errno);
    }
    return String::from(StringRef(buf), alloc);
#endif
}

Error fs_set_current_dir(StringRef path) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    if (!SetCurrentDirectoryW(wpath.data())) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return Error::ok();
#else
    ArenaTemp scratch = get_scratch();
    String cpath = String::from(path, scratch.allocator());
    if (::chdir(cpath.c_str()) != 0) {
        return detail::errno_to_keel(errno);
    }
    return Error::ok();
#endif
}

namespace {

[[nodiscard]] String normalize_dir_result(StringRef raw, Allocator* alloc) {
    String out = String::from(raw, alloc);
    for (usize i = 0; i < out.size(); ++i) {
        if (out[i] == '\\') out[i] = '/';
    }
    const usize trimmed = detail::strip_trailing_seps(out.as_ref());
    if (trimmed < out.size()) {
        out.resize(trimmed);
    }
    return out;
}

} // namespace

Result<String> fs_temp_dir(Allocator* alloc) {
#if defined(_WIN32)
    wchar_t wbuf[MAX_PATH + 2];
    const DWORD len = GetTempPathW(MAX_PATH + 1, wbuf);
    if (len == 0) return detail::win32_error_to_keel(GetLastError());
    String out(alloc);
    detail::win32_wide_to_utf8_path(wbuf, as<usize>(len), out);
    if (out.size() > 1 && out[out.size() - 1] == '/') out.pop_back();
    return out;
#else
    const char* env_tmp = std::getenv("TMPDIR");
    if (!env_tmp || env_tmp[0] == '\0') env_tmp = "/tmp";
    return normalize_dir_result(StringRef(env_tmp), alloc);
#endif
}

Result<String> fs_home_dir(Allocator* alloc) {
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch(alloc);
    if (auto up = env_get("USERPROFILE", scratch.allocator()); up.has_value() && !up->empty()) {
        return normalize_dir_result(up->as_ref(), alloc);
    }
    auto hd = env_get("HOMEDRIVE", scratch.allocator());
    auto hp = env_get("HOMEPATH", scratch.allocator());
    if (hd.has_value() && hp.has_value() && !hd->empty() && !hp->empty()) {
        String combined(scratch.allocator());
        combined.reserve(hd->size() + hp->size());
        combined.append(hd->as_ref());
        combined.append(hp->as_ref());
        return normalize_dir_result(combined.as_ref(), alloc);
    }
    if (auto h = env_get("HOME", scratch.allocator()); h.has_value() && !h->empty()) {
        return normalize_dir_result(h->as_ref(), alloc);
    }
    return Error{ErrorCode::NotFound, 0};
#elif defined(__EMSCRIPTEN__)
    ArenaTemp scratch = get_scratch(alloc);
    if (auto h = env_get("HOME", scratch.allocator()); h.has_value() && !h->empty()) {
        return normalize_dir_result(h->as_ref(), alloc);
    }
    return String::from("/", alloc);
#else
    ArenaTemp scratch = get_scratch(alloc);
    if (auto h = env_get("HOME", scratch.allocator()); h.has_value() && !h->empty()) {
        return normalize_dir_result(h->as_ref(), alloc);
    }
    struct passwd pw {};
    struct passwd* result = nullptr;
    char buf[4096];
    if (::getpwuid_r(::getuid(), &pw, buf, sizeof(buf), &result) == 0 && result && result->pw_dir &&
        result->pw_dir[0] != '\0') {
        return normalize_dir_result(StringRef(result->pw_dir), alloc);
    }
    return Error{ErrorCode::NotFound, 0};
#endif
}

Result<String> fs_config_dir(Allocator* alloc) {
    ArenaTemp scratch = get_scratch(alloc);
#if defined(_WIN32)
    if (auto ad = env_get("APPDATA", scratch.allocator()); ad.has_value() && !ad->empty()) {
        return normalize_dir_result(ad->as_ref(), alloc);
    }
    KEEL_TRY_ASSIGN(String home, fs_home_dir(scratch.allocator()));
    return path_join(home.as_ref(), "AppData/Roaming", alloc);
#elif defined(__APPLE__)
    KEEL_TRY_ASSIGN(String home, fs_home_dir(scratch.allocator()));
    return path_join(home.as_ref(), "Library/Application Support", alloc);
#else
    if (auto xdg = env_get("XDG_CONFIG_HOME", scratch.allocator());
        xdg.has_value() && !xdg->empty() && path_is_absolute(xdg->as_ref())) {
        return normalize_dir_result(xdg->as_ref(), alloc);
    }
    KEEL_TRY_ASSIGN(String home, fs_home_dir(scratch.allocator()));
    return path_join(home.as_ref(), ".config", alloc);
#endif
}

Result<String> fs_data_dir(Allocator* alloc) {
    ArenaTemp scratch = get_scratch(alloc);
#if defined(_WIN32)
    if (auto lad = env_get("LOCALAPPDATA", scratch.allocator()); lad.has_value() && !lad->empty()) {
        return normalize_dir_result(lad->as_ref(), alloc);
    }
    KEEL_TRY_ASSIGN(String home, fs_home_dir(scratch.allocator()));
    return path_join(home.as_ref(), "AppData/Local", alloc);
#elif defined(__APPLE__)
    KEEL_TRY_ASSIGN(String home, fs_home_dir(scratch.allocator()));
    return path_join(home.as_ref(), "Library/Application Support", alloc);
#else
    if (auto xdg = env_get("XDG_DATA_HOME", scratch.allocator());
        xdg.has_value() && !xdg->empty() && path_is_absolute(xdg->as_ref())) {
        return normalize_dir_result(xdg->as_ref(), alloc);
    }
    KEEL_TRY_ASSIGN(String home, fs_home_dir(scratch.allocator()));
    return path_join(home.as_ref(), ".local/share", alloc);
#endif
}

Result<String> fs_cache_dir(Allocator* alloc) {
    ArenaTemp scratch = get_scratch(alloc);
#if defined(_WIN32)
    if (auto lad = env_get("LOCALAPPDATA", scratch.allocator()); lad.has_value() && !lad->empty()) {
        return normalize_dir_result(lad->as_ref(), alloc);
    }
    KEEL_TRY_ASSIGN(String home, fs_home_dir(scratch.allocator()));
    return path_join(home.as_ref(), "AppData/Local", alloc);
#elif defined(__APPLE__)
    KEEL_TRY_ASSIGN(String home, fs_home_dir(scratch.allocator()));
    return path_join(home.as_ref(), "Library/Caches", alloc);
#else
    if (auto xdg = env_get("XDG_CACHE_HOME", scratch.allocator());
        xdg.has_value() && !xdg->empty() && path_is_absolute(xdg->as_ref())) {
        return normalize_dir_result(xdg->as_ref(), alloc);
    }
    KEEL_TRY_ASSIGN(String home, fs_home_dir(scratch.allocator()));
    return path_join(home.as_ref(), ".cache", alloc);
#endif
}

Result<String> fs_exe_path(Allocator* alloc) {
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch(alloc);
    SmallArray<wchar_t, 260> wbuf(scratch.allocator());
    DWORD cap = 260;
    for (;;) {
        wbuf.reserve(as<usize>(cap));
        const DWORD len = GetModuleFileNameW(nullptr, wbuf.data(), cap);
        if (len == 0) return detail::win32_error_to_keel(GetLastError());
        if (len < cap) {
            String out(alloc);
            detail::win32_wide_to_utf8_path(wbuf.data(), as<usize>(len), out);
            return out;
        }
        cap *= 2;
    }
#elif defined(__APPLE__)
    uint32_t sz = 0;
    _NSGetExecutablePath(nullptr, &sz);
    ArenaTemp scratch = get_scratch(alloc);
    String buf(scratch.allocator());
    buf.resize(as<usize>(sz));
    if (_NSGetExecutablePath(buf.data(), &sz) != 0) {
        return Error{ErrorCode::IoError, 0};
    }
    return String::from(StringRef(buf.c_str()), alloc);
#elif defined(__EMSCRIPTEN__)
    return String::from("/", alloc);
#else
    char buf[4096];
    const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len < 0) return detail::errno_to_keel(errno);
    buf[len] = '\0';
    return String::from(StringRef(buf, as<usize>(len)), alloc);
#endif
}

Result<String> fs_exe_dir(Allocator* alloc) {
    ArenaTemp scratch = get_scratch(alloc);
    KEEL_TRY_ASSIGN(String exe, fs_exe_path(scratch.allocator()));
    return String::from(path_dirname(exe.as_ref()), alloc);
}

Result<String> fs_absolute(StringRef path, Allocator* alloc) {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch(alloc);
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    const DWORD needed = GetFullPathNameW(wpath.data(), 0, nullptr, nullptr);
    if (needed == 0) return detail::win32_error_to_keel(GetLastError());
    SmallArray<wchar_t, 260> wbuf(scratch.allocator());
    wbuf.reserve(as<usize>(needed) + 1);
    const DWORD len = GetFullPathNameW(wpath.data(), needed, wbuf.data(), nullptr);
    if (len == 0) return detail::win32_error_to_keel(GetLastError());
    String out(alloc);
    detail::win32_wide_to_utf8_path(wbuf.data(), as<usize>(len), out);
    return out;
#else
    if (path_is_absolute(path)) {
        return path_normalize(path, alloc);
    }
    ArenaTemp scratch = get_scratch(alloc);
    KEEL_TRY_ASSIGN(String cwd, fs_current_dir(scratch.allocator()));
    PathBuf joined(scratch.allocator());
    path_join_into(joined, cwd, path);
    return path_normalize(joined, alloc);
#endif
}

Result<String> fs_realpath(StringRef path, Allocator* alloc) {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch(alloc);
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    HANDLE hFile = CreateFileW(wpath.data(), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return detail::win32_error_to_keel(GetLastError());
    }
    const DWORD needed = GetFinalPathNameByHandleW(hFile, nullptr, 0, VOLUME_NAME_DOS);
    if (needed == 0) {
        const DWORD err = GetLastError();
        CloseHandle(hFile);
        return detail::win32_error_to_keel(err);
    }
    SmallArray<wchar_t, 260> wbuf(scratch.allocator());
    wbuf.reserve(as<usize>(needed) + 1);
    const DWORD len = GetFinalPathNameByHandleW(hFile, wbuf.data(), needed, VOLUME_NAME_DOS);
    const DWORD last_err = GetLastError();
    CloseHandle(hFile);
    if (len == 0) {
        return detail::win32_error_to_keel(last_err);
    }
    const wchar_t* start = wbuf.data();
    usize wlen = as<usize>(len);
    String out(alloc);
    if (wlen >= 8 && start[0] == L'\\' && start[1] == L'\\' && start[2] == L'?' && start[3] == L'\\' &&
        (start[4] == L'U' || start[4] == L'u') && (start[5] == L'N' || start[5] == L'n') &&
        (start[6] == L'C' || start[6] == L'c') && start[7] == L'\\') {
        out.append("//");
        String tail(scratch.allocator());
        detail::win32_wide_to_utf8_path(start + 8, wlen - 8, tail);
        out.append(tail);
        return out;
    }
    if (wlen >= 4 && start[0] == L'\\' && start[1] == L'\\' && start[2] == L'?' && start[3] == L'\\') {
        start += 4;
        wlen -= 4;
    }
    detail::win32_wide_to_utf8_path(start, wlen, out);
    return out;
#else
    PathBuf cpath(path);
    char* rp = ::realpath(cpath.c_str(), nullptr);
    if (!rp) {
        return detail::errno_to_keel(errno);
    }
    String out = String::from(StringRef(rp), alloc);
    std::free(rp);
    return out;
#endif
}

Error fs_read_dir(StringRef dir_path, FunctionRef<bool(const DirEntry&)> callback) {
    if (dir_path.empty() || !callback) return Error{ErrorCode::InvalidArgument, 0};

#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    String pattern = path_join(dir_path, "*", scratch.allocator());
    SmallArray<wchar_t, 260> wpattern(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(pattern, wpattern)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }

    WIN32_FIND_DATAW fdata{};
    HANDLE hFind = FindFirstFileW(wpattern.data(), &fdata);
    if (hFind == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) return Error::ok();
        return detail::win32_error_to_keel(err);
    }

    do {
        if (fdata.cFileName[0] == L'.' &&
            (fdata.cFileName[1] == L'\0' || (fdata.cFileName[1] == L'.' && fdata.cFileName[2] == L'\0'))) {
            continue;
        }
        ArenaTemp iter_scratch = scratch.arena().begin_temp();
        String name_str(iter_scratch.allocator());
        String full_str(iter_scratch.allocator());
        usize wlen = 0;
        while (fdata.cFileName[wlen] != L'\0') ++wlen;
        detail::win32_wide_to_utf8_path(fdata.cFileName, wlen, name_str);
        path_join_into(full_str, dir_path, name_str);

        DirEntry entry{};
        entry.name = name_str.as_ref();
        entry.full_path = full_str.as_ref();
        entry.is_dir = (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.is_file = !entry.is_dir;

        if (!callback(entry)) {
            break;
        }
    } while (FindNextFileW(hFind, &fdata));

    FindClose(hFind);
    return Error::ok();
#else
    ArenaTemp scratch = get_scratch();
    String cdir = String::from(dir_path, scratch.allocator());
    DIR* dir = ::opendir(cdir.c_str());
    if (!dir) {
        return detail::errno_to_keel(errno);
    }

    while (struct dirent* ent = ::readdir(dir)) {
        const StringRef name(ent->d_name);
        if (name == "." || name == "..") continue;

        ArenaTemp iter_scratch = scratch.arena().begin_temp();
        String full_str(iter_scratch.allocator());
        path_join_into(full_str, dir_path, name);

        DirEntry entry{};
        entry.name = name;
        entry.full_path = full_str.as_ref();
#ifdef _DIRENT_HAVE_D_TYPE
        if (ent->d_type == DT_DIR) {
            entry.is_dir = true;
        } else if (ent->d_type == DT_REG) {
            entry.is_file = true;
        } else
#endif
        {
            auto st = fs_stat(full_str);
            if (st.is_ok()) {
                entry.is_dir = st->is_dir;
                entry.is_file = st->is_file;
            }
        }

        if (!callback(entry)) {
            break;
        }
    }
    ::closedir(dir);
    return Error::ok();
#endif
}

namespace {
Error walk_dir_impl(StringRef dir_path, FunctionRef<bool(const DirEntry&)> callback, bool& keep_going) {
#if defined(_WIN32)
    PathBuf pattern;
    path_join_into(pattern, dir_path, "*");
    SmallArray<wchar_t, 260> wpattern;
    if (!detail::utf8_path_to_win32_wide(pattern, wpattern)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }

    WIN32_FIND_DATAW fdata{};
    HANDLE hFind = FindFirstFileW(wpattern.data(), &fdata);
    if (hFind == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) return Error::ok();
        return detail::win32_error_to_keel(err);
    }

    Error result = Error::ok();
    do {
        if (fdata.cFileName[0] == L'.' &&
            (fdata.cFileName[1] == L'\0' || (fdata.cFileName[1] == L'.' && fdata.cFileName[2] == L'\0'))) {
            continue;
        }
        String name_str;
        usize wlen = 0;
        while (fdata.cFileName[wlen] != L'\0') ++wlen;
        detail::win32_wide_to_utf8_path(fdata.cFileName, wlen, name_str);

        PathBuf full_str;
        path_join_into(full_str, dir_path, name_str);

        const bool is_dir = (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool is_reparse = (fdata.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        DirEntry entry{};
        entry.name = name_str.as_ref();
        entry.full_path = full_str.as_ref();
        entry.is_dir = is_dir;
        entry.is_file = !is_dir;

        if (!callback(entry)) {
            keep_going = false;
            break;
        }
        if (is_dir && !is_reparse) {
            result = walk_dir_impl(full_str, callback, keep_going);
            if (result.is_err() || !keep_going) {
                break;
            }
        }
    } while (FindNextFileW(hFind, &fdata));

    FindClose(hFind);
    return result;
#else
    PathBuf cdir(dir_path);
    DIR* dir = ::opendir(cdir.c_str());
    if (!dir) {
        return detail::errno_to_keel(errno);
    }

    Error result = Error::ok();
    while (struct dirent* ent = ::readdir(dir)) {
        const StringRef name(ent->d_name);
        if (name == "." || name == "..") continue;

        PathBuf full_str;
        path_join_into(full_str, dir_path, name);

        bool recurse_subdir = false;
        DirEntry entry{};
        entry.name = name;
        entry.full_path = full_str.as_ref();

        struct stat lst {};
        if (::lstat(full_str.c_str(), &lst) == 0) {
            if (S_ISDIR(lst.st_mode)) {
                entry.is_dir = true;
                recurse_subdir = true;
            } else if (S_ISREG(lst.st_mode)) {
                entry.is_file = true;
            } else if (S_ISLNK(lst.st_mode)) {
                struct stat st {};
                if (::stat(full_str.c_str(), &st) == 0) {
                    entry.is_dir = S_ISDIR(st.st_mode);
                    entry.is_file = S_ISREG(st.st_mode);
                }
            }
        }

        if (!callback(entry)) {
            keep_going = false;
            break;
        }
        if (recurse_subdir) {
            result = walk_dir_impl(full_str, callback, keep_going);
            if (result.is_err() || !keep_going) {
                break;
            }
        }
    }
    ::closedir(dir);
    return result;
#endif
}
} // namespace

Error fs_walk_dir(StringRef dir_path, FunctionRef<bool(const DirEntry&)> callback) {
    if (dir_path.empty() || !callback) return Error{ErrorCode::InvalidArgument, 0};
    bool keep_going = true;
    return walk_dir_impl(dir_path, callback, keep_going);
}

Error fs_set_mtime_now(StringRef path) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    HANDLE h = CreateFileW(wpath.data(), FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return detail::win32_error_to_keel(GetLastError());
    }
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const BOOL ok = SetFileTime(h, nullptr, &ft, &ft);
    const DWORD err = ok ? 0 : GetLastError();
    CloseHandle(h);
    return ok ? Error::ok() : detail::win32_error_to_keel(err);
#else
    ArenaTemp scratch = get_scratch();
    String cpath = String::from(path, scratch.allocator());
    struct timespec ts[2];
    if (::clock_gettime(CLOCK_REALTIME, &ts[0]) != 0) {
        return detail::errno_to_keel(errno);
    }
    ts[1] = ts[0];
    if (::utimensat(AT_FDCWD, cpath.c_str(), ts, 0) != 0) {
        return detail::errno_to_keel(errno);
    }
    return Error::ok();
#endif
}

Result<String> fs_create_temp_dir(StringRef prefix, Allocator* alloc) {
    for (usize i = 0; i < prefix.size(); ++i) {
        if (prefix[i] == '/' || prefix[i] == '\\') {
            return Error{ErrorCode::InvalidArgument, 0};
        }
    }
    const StringRef pfx = prefix.empty() ? StringRef("keel_") : prefix;
    ArenaTemp scratch = get_scratch(alloc);
    KEEL_TRY_ASSIGN(String tmp_root, fs_temp_dir(scratch.allocator()));

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    String leaf(scratch.allocator());
    leaf.reserve(pfx.size() + 6);
    leaf.append(pfx);
    leaf.append("XXXXXX");
    String tmpl = path_join(tmp_root.as_ref(), leaf.as_ref(), scratch.allocator());
    if (!::mkdtemp(tmpl.data())) {
        return detail::errno_to_keel(errno);
    }
    return path_normalize(tmpl.as_ref(), alloc);
#else
    static std::atomic<u64> s_temp_dir_seq{0};
    const u64 pid = process_id();
    for (u32 attempt = 0; attempt < 64u; ++attempt) {
        const u64 seq = s_temp_dir_seq.fetch_add(1, std::memory_order_relaxed);
        const u64 ns = monotonic_ns();
        char hex_buf[32];
        String leaf(scratch.allocator());
        leaf.reserve(pfx.size() + 48);
        leaf.append(pfx);
        leaf.append(StringRef(hex_buf, format_u64(hex_buf, sizeof(hex_buf), pid, 16, false)));
        leaf.push_back('_');
        leaf.append(StringRef(hex_buf, format_u64(hex_buf, sizeof(hex_buf), ns, 16, false)));
        leaf.push_back('_');
        leaf.append(StringRef(hex_buf, format_u64(hex_buf, sizeof(hex_buf), seq, 16, false)));
        String cand = path_join(tmp_root.as_ref(), leaf.as_ref(), scratch.allocator());
        const Error err = fs_create_dir(cand.as_ref());
        if (err.is_ok()) {
            return path_normalize(cand.as_ref(), alloc);
        }
        if (err.code != ErrorCode::AlreadyExists) {
            return err;
        }
    }
    return Error{ErrorCode::AlreadyExists, 0};
#endif
}

} // namespace keel
