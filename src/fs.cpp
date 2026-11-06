#include "keel/fs.h"
#include "keel/arena.h"
#include "keel/io.h"
#include "keel/small_array.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
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
void win32_wide_to_utf8_path(const wchar_t* wstr, isize wlen, String& out);
#else
Error errno_to_keel(int err) noexcept;
#endif

constexpr bool is_sep(char c) noexcept { return c == '/' || c == '\\'; }

constexpr isize strip_trailing_seps(StringRef path) noexcept {
    isize n = path.size();
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
    const isize len = detail::strip_trailing_seps(path);
    const StringRef trimmed = path.substr(0, len);

    isize last_sep = kInvalidIndex;
    for (isize i = trimmed.size() - 1; i >= 0; --i) {
        if (detail::is_sep(trimmed[i])) {
            last_sep = i;
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
    isize end = path.size();
    while (end > 0 && detail::is_sep(path[end - 1])) {
        --end;
    }
    if (end == 0) return path.substr(0, 1);

    isize start = end;
    while (start > 0 && !detail::is_sep(path[start - 1])) {
        --start;
    }
    return path.substr(start, end - start);
}

StringRef path_extension(StringRef path) noexcept {
    const StringRef base = path_basename(path);
    if (base.empty() || base == "." || base == "..") return "";
    const isize dot = base.rfind('.');
    if (dot <= 0) return ""; // Leading dot `.gitignore` has no extension
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

void path_join_into(String& out, StringRef a, StringRef b) {
    if (b.empty()) {
        out.append(a);
        return;
    }
    if (a.empty() || path_is_absolute(b)) {
        out.append(b);
        return;
    }
    out.reserve(out.size() + a.size() + 1 + b.size());
    out.append(a);
    if (!detail::is_sep(a.back())) {
        out.push_back('/');
    }
    isize b_start = 0;
    while (b_start < b.size() && detail::is_sep(b[b_start])) {
        ++b_start;
    }
    out.append(b.substr(b_start));
    for (isize i = 0; i < out.size(); ++i) {
        if (out[i] == '\\') out[i] = '/';
    }
}

String path_join(StringRef a, StringRef b, Allocator* alloc) {
    String out(alloc);
    path_join_into(out, a, b);
    return out;
}

void path_normalize_into(String& out, StringRef path) {
    if (path.empty()) {
        out.append(".");
        return;
    }

    ArenaTemp scratch = get_scratch();
    SmallArray<StringRef, 32> parts(scratch.allocator());

    StringRef prefix{};
    isize i = 0;
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
        const isize start = i;
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
    for (isize k = 0; k < parts.size(); ++k) {
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
    st.size = (static_cast<u64>(data.nFileSizeHigh) << 32) | static_cast<u64>(data.nFileSizeLow);
    const i64 ticks = (static_cast<i64>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                      static_cast<i64>(data.ftLastWriteTime.dwLowDateTime);
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
    out.size = static_cast<u64>(st.st_size);
#if defined(__APPLE__)
    out.mtime_ns = static_cast<i64>(st.st_mtimespec.tv_sec) * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
    out.mtime_ns = static_cast<i64>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
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
    buf.resize(static_cast<isize>(sz));
    isize total = 0;
    while (total < buf.size()) {
        KEEL_TRY_ASSIGN(isize n, f.read(MutArrayRef<u8>(buf.data() + total, buf.size() - total)));
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
    text.resize(static_cast<isize>(sz));
    isize total = 0;
    while (total < text.size()) {
        KEEL_TRY_ASSIGN(
            isize n, f.read(MutArrayRef<u8>(reinterpret_cast<u8*>(text.data() + total), text.size() - total)));
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

    for (isize i = 1; i <= norm.size(); ++i) {
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
        KEEL_TRY_ASSIGN(isize n, src.read(MutArrayRef<u8>(buf, sizeof(buf))));
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
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wbuf(scratch.allocator());
    wbuf.reserve(static_cast<isize>(needed) + 1);
    const DWORD len = GetCurrentDirectoryW(needed, wbuf.data());
    if (len == 0) return detail::win32_error_to_keel(GetLastError());
    String out(alloc);
    detail::win32_wide_to_utf8_path(wbuf.data(), static_cast<isize>(len), out);
    return out;
#else
    char buf[4096];
    if (!::getcwd(buf, sizeof(buf))) {
        return detail::errno_to_keel(errno);
    }
    return String::from(StringRef(buf), alloc);
#endif
}

Result<String> fs_temp_dir(Allocator* alloc) {
#if defined(_WIN32)
    wchar_t wbuf[MAX_PATH + 2];
    const DWORD len = GetTempPathW(MAX_PATH + 1, wbuf);
    if (len == 0) return detail::win32_error_to_keel(GetLastError());
    String out(alloc);
    detail::win32_wide_to_utf8_path(wbuf, static_cast<isize>(len), out);
    if (out.size() > 1 && out[out.size() - 1] == '/') out.pop_back();
    return out;
#else
    const char* env_tmp = std::getenv("TMPDIR");
    if (!env_tmp || env_tmp[0] == '\0') env_tmp = "/tmp";
    return String::from(StringRef(env_tmp), alloc);
#endif
}

Result<String> fs_exe_path(Allocator* alloc) {
#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wbuf(scratch.allocator());
    DWORD cap = 260;
    for (;;) {
        wbuf.reserve(static_cast<isize>(cap));
        const DWORD len = GetModuleFileNameW(nullptr, wbuf.data(), cap);
        if (len == 0) return detail::win32_error_to_keel(GetLastError());
        if (len < cap) {
            String out(alloc);
            detail::win32_wide_to_utf8_path(wbuf.data(), static_cast<isize>(len), out);
            return out;
        }
        cap *= 2;
    }
#elif defined(__APPLE__)
    uint32_t sz = 0;
    _NSGetExecutablePath(nullptr, &sz);
    ArenaTemp scratch = get_scratch();
    String buf(scratch.allocator());
    buf.resize(static_cast<isize>(sz));
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
    return String::from(StringRef(buf, static_cast<isize>(len)), alloc);
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

    String name_str(scratch.allocator());
    String full_str(scratch.allocator());
    do {
        if (fdata.cFileName[0] == L'.' &&
            (fdata.cFileName[1] == L'\0' || (fdata.cFileName[1] == L'.' && fdata.cFileName[2] == L'\0'))) {
            continue;
        }
        isize wlen = 0;
        while (fdata.cFileName[wlen] != L'\0') ++wlen;
        detail::win32_wide_to_utf8_path(fdata.cFileName, wlen, name_str);
        full_str.clear();
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

    String full_str(scratch.allocator());
    while (struct dirent* ent = ::readdir(dir)) {
        const StringRef name(ent->d_name);
        if (name == "." || name == "..") continue;

        full_str.clear();
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

} // namespace keel
