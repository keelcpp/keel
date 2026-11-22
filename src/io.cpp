#include "keel/io.h"
#include "keel/arena.h"
#include "keel/small_array.h"
#include "keel/utf8.h"

#include <cstdio>

#if defined(_WIN32)
#include "win32_compat.h"
#else
#include <cerrno>
#include <fcntl.h>
#if !defined(__EMSCRIPTEN__)
#include <sys/file.h>
#include <sys/ioctl.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace keel {

File file_from_native_handle_internal(i64 handle) noexcept { return File(handle); }

namespace detail {

#if defined(_WIN32)
Error win32_error_to_keel(DWORD err) noexcept {
    switch (err) {
    case ERROR_SUCCESS:
        return Error::ok();
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return Error{ErrorCode::NotFound, as<i32>(err)};
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return Error{ErrorCode::PermissionDenied, as<i32>(err)};
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return Error{ErrorCode::AlreadyExists, as<i32>(err)};
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
        return Error{ErrorCode::InvalidArgument, as<i32>(err)};
    case ERROR_HANDLE_EOF:
        return Error{ErrorCode::EndOfFile, as<i32>(err)};
    default:
        return Error{ErrorCode::IoError, as<i32>(err)};
    }
}

// Converts UTF-8 path (with `/` or `\`) into null-terminated UTF-16 `wchar_t` buffer in `out`.
bool utf8_path_to_win32_wide(StringRef utf8_path, SmallArray<wchar_t, 260>& out) {
    if (utf8_path.empty() || !utf8_validate(utf8_path)) return false;

    const int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path.data(),
                                         as<int>(utf8_path.size()), nullptr, 0);
    if (wlen <= 0) return false;

    const bool needs_long_prefix = (wlen >= MAX_PATH - 12) && !utf8_path.starts_with("\\\\");
    const usize prefix_len = needs_long_prefix ? 4 : 0;
    out.clear();
    out.reserve(prefix_len + as<usize>(wlen) + 1);
    if (needs_long_prefix) {
        out.push_back(L'\\');
        out.push_back(L'\\');
        out.push_back(L'?');
        out.push_back(L'\\');
    }
    for (int i = 0; i < wlen; ++i) {
        out.push_back(L'\0');
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path.data(), as<int>(utf8_path.size()),
                        out.data() + prefix_len, wlen);
    for (usize i = prefix_len; i < out.size(); ++i) {
        if (out[i] == L'/') out[i] = L'\\';
    }
    out.push_back(L'\0');
    return true;
}

void win32_wide_to_utf8_path(const wchar_t* wstr, usize wlen, String& out) {
    out.clear();
    if (!wstr || wlen == 0) return;
    // Strip `\\?\` prefix if present
    if (wlen >= 4 && wstr[0] == L'\\' && wstr[1] == L'\\' && wstr[2] == L'?' && wstr[3] == L'\\') {
        wstr += 4;
        wlen -= 4;
    }
    const int u8len =
        WideCharToMultiByte(CP_UTF8, 0, wstr, as<int>(wlen), nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return;
    out.resize(as<usize>(u8len));
    WideCharToMultiByte(CP_UTF8, 0, wstr, as<int>(wlen), out.data(), u8len, nullptr, nullptr);
    for (usize i = 0; i < out.size(); ++i) {
        if (out[i] == '\\') out[i] = '/';
    }
}
#else
Error errno_to_keel(int err) noexcept {
    switch (err) {
    case 0:
        return Error::ok();
    case ENOENT:
        return Error{ErrorCode::NotFound, err};
    case EACCES:
    case EPERM:
        return Error{ErrorCode::PermissionDenied, err};
    case EEXIST:
        return Error{ErrorCode::AlreadyExists, err};
    case EINVAL:
    case ENAMETOOLONG:
        return Error{ErrorCode::InvalidArgument, err};
    case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
        return Error{ErrorCode::WouldBlock, err};
    case ENOSYS:
    case ENOTSUP:
        return Error{ErrorCode::NotSupported, err};
    default:
        return Error{ErrorCode::IoError, err};
    }
}
#endif

} // namespace detail

File::~File() noexcept { (void)close(); }

File::File(File&& other) noexcept : m_handle(other.m_handle), m_sticky_error(other.m_sticky_error) {
    other.m_handle = kInvalidHandle;
    other.m_sticky_error = Error::ok();
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        (void)close();
        m_handle = other.m_handle;
        m_sticky_error = other.m_sticky_error;
        other.m_handle = kInvalidHandle;
        other.m_sticky_error = Error::ok();
    }
    return *this;
}

Result<File> File::open(StringRef path, FileMode mode) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};

#if defined(_WIN32)
    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }

    DWORD access = 0;
    DWORD creation = 0;
    switch (mode) {
    case FileMode::Read:
        access = GENERIC_READ;
        creation = OPEN_EXISTING;
        break;
    case FileMode::Write:
        access = GENERIC_WRITE;
        creation = CREATE_ALWAYS;
        break;
    case FileMode::Append:
        access = FILE_APPEND_DATA;
        creation = OPEN_ALWAYS;
        break;
    case FileMode::ReadWrite:
        access = GENERIC_READ | GENERIC_WRITE;
        creation = OPEN_ALWAYS;
        break;
    }

    HANDLE h = CreateFileW(wpath.data(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return File(reinterpret_cast<i64>(h));
#else
    ArenaTemp scratch = get_scratch();
    String cpath = String::from(path, scratch.allocator());

    int flags = 0;
    switch (mode) {
    case FileMode::Read:
        flags = O_RDONLY;
        break;
    case FileMode::Write:
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        break;
    case FileMode::Append:
        flags = O_WRONLY | O_CREAT | O_APPEND;
        break;
    case FileMode::ReadWrite:
        flags = O_RDWR | O_CREAT;
        break;
    }
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int fd = ::open(cpath.c_str(), flags, 0666);
    if (fd < 0) {
        return detail::errno_to_keel(errno);
    }
    return File(as<i64>(fd));
#endif
}

Result<usize> File::read(MutArrayRef<u8> buf) noexcept {
    if (!is_open()) return Error{ErrorCode::InvalidArgument, 0};
    if (buf.empty()) return usize{0};

#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    const DWORD to_read = (buf.size() > 0x7FFFFFFFu) ? 0x7FFFFFFFu : static_cast<DWORD>(buf.size());
    DWORD bytes_read = 0;
    if (!ReadFile(h, buf.data(), to_read, &bytes_read, nullptr)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return as<usize>(bytes_read);
#else
    const int fd = as<int>(m_handle);
    const ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n < 0) {
        return detail::errno_to_keel(errno);
    }
    return as<usize>(n);
#endif
}

void File::write(ArrayRef<u8> data) noexcept {
    if (m_sticky_error.is_err()) return;
    if (!is_open()) {
        m_sticky_error = Error{ErrorCode::InvalidArgument, 0};
        return;
    }
    if (data.empty()) return;

#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    const u8* p = data.data();
    usize rem = data.size();
    while (rem > 0) {
        const DWORD chunk = (rem > 0x7FFFFFFFu) ? 0x7FFFFFFFu : static_cast<DWORD>(rem);
        DWORD written = 0;
        if (!WriteFile(h, p, chunk, &written, nullptr)) {
            m_sticky_error = detail::win32_error_to_keel(GetLastError());
            return;
        }
        p += written;
        rem -= as<usize>(written);
    }
#else
    const int fd = as<int>(m_handle);
    const u8* p = data.data();
    usize rem = data.size();
    while (rem > 0) {
        const ssize_t n = ::write(fd, p, rem);
        if (n < 0) {
            if (errno == EINTR) continue;
            m_sticky_error = detail::errno_to_keel(errno);
            return;
        }
        p += n;
        rem -= as<usize>(n);
    }
#endif
}

Error File::flush() noexcept {
    if (m_sticky_error.is_err()) return m_sticky_error;
    if (!is_open()) return Error{ErrorCode::InvalidArgument, 0};

#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    if (!FlushFileBuffers(h)) {
        m_sticky_error = detail::win32_error_to_keel(GetLastError());
        return m_sticky_error;
    }
    return Error::ok();
#else
    const int fd = as<int>(m_handle);
    if (::fsync(fd) != 0) {
        m_sticky_error = detail::errno_to_keel(errno);
        return m_sticky_error;
    }
    return Error::ok();
#endif
}

Result<i64> File::seek(i64 offset, SeekOrigin origin) noexcept {
    if (!is_open()) return Error{ErrorCode::InvalidArgument, 0};

#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    DWORD method = FILE_BEGIN;
    if (origin == SeekOrigin::Current) method = FILE_CURRENT;
    else if (origin == SeekOrigin::End) method = FILE_END;
    LARGE_INTEGER li_off{};
    li_off.QuadPart = offset;
    LARGE_INTEGER li_new{};
    if (!SetFilePointerEx(h, li_off, &li_new, method)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return as<i64>(li_new.QuadPart);
#else
    const int fd = as<int>(m_handle);
    int whence = SEEK_SET;
    if (origin == SeekOrigin::Current) whence = SEEK_CUR;
    else if (origin == SeekOrigin::End) whence = SEEK_END;
    const off_t pos = ::lseek(fd, static_cast<off_t>(offset), whence);
    if (pos == static_cast<off_t>(-1)) {
        return detail::errno_to_keel(errno);
    }
    return as<i64>(pos);
#endif
}

Result<i64> File::tell() const noexcept {
    if (!is_open()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    LARGE_INTEGER zero{};
    LARGE_INTEGER pos{};
    if (!SetFilePointerEx(h, zero, &pos, FILE_CURRENT)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return as<i64>(pos.QuadPart);
#else
    const int fd = as<int>(m_handle);
    const off_t pos = ::lseek(fd, 0, SEEK_CUR);
    if (pos == static_cast<off_t>(-1)) {
        return detail::errno_to_keel(errno);
    }
    return as<i64>(pos);
#endif
}

Result<i64> File::size() const noexcept {
    if (!is_open()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return as<i64>(sz.QuadPart);
#else
    const int fd = as<int>(m_handle);
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        return detail::errno_to_keel(errno);
    }
    return as<i64>(st.st_size);
#endif
}

Error File::close() noexcept {
    const Error sticky = m_sticky_error;
    m_sticky_error = Error::ok();
    if (!is_open()) return sticky;

#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    m_handle = kInvalidHandle;
    if (!CloseHandle(h)) {
        const Error close_err = detail::win32_error_to_keel(GetLastError());
        return sticky.is_err() ? sticky : close_err;
    }
    return sticky;
#else
    const int fd = as<int>(m_handle);
    m_handle = kInvalidHandle;
    if (::close(fd) != 0) {
        const Error close_err = detail::errno_to_keel(errno);
        return sticky.is_err() ? sticky : close_err;
    }
    return sticky;
#endif
}

void stdout_write(StringRef str) noexcept {
    if (!str.empty()) {
        (void)std::fwrite(str.data(), 1, str.size(), stdout);
    }
}

void stderr_write(StringRef str) noexcept {
    if (!str.empty()) {
        (void)std::fwrite(str.data(), 1, str.size(), stderr);
    }
}

void stdout_flush() noexcept { (void)std::fflush(stdout); }
void stderr_flush() noexcept { (void)std::fflush(stderr); }

Result<usize> File::pread(MutArrayRef<u8> buf, i64 offset) const noexcept {
    if (!is_open() || offset < 0) return Error{ErrorCode::InvalidArgument, 0};
    if (buf.empty()) return usize{0};

#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<isize>(m_handle));
    const DWORD to_read = (buf.size() > 0x7FFFFFFFu) ? 0x7FFFFFFFu : static_cast<DWORD>(buf.size());
    OVERLAPPED ov{};
    const u64 uoff = static_cast<u64>(offset);
    ov.Offset = static_cast<DWORD>(uoff & 0xFFFFFFFFULL);
    ov.OffsetHigh = static_cast<DWORD>(uoff >> 32);
    DWORD bytes_read = 0;
    if (!ReadFile(h, buf.data(), to_read, &bytes_read, &ov)) {
        const DWORD e = GetLastError();
        if (e == ERROR_HANDLE_EOF) return usize{0};
        return detail::win32_error_to_keel(e);
    }
    return as<usize>(bytes_read);
#else
    const int fd = as<int>(m_handle);
    for (;;) {
        const ssize_t n = ::pread(fd, buf.data(), buf.size(), static_cast<off_t>(offset));
        if (n < 0) {
            if (errno == EINTR) continue;
            return detail::errno_to_keel(errno);
        }
        return as<usize>(n);
    }
#endif
}

Result<usize> File::pwrite(ArrayRef<u8> data, i64 offset) noexcept {
    if (!is_open() || offset < 0) return Error{ErrorCode::InvalidArgument, 0};
    if (data.empty()) return usize{0};

#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<isize>(m_handle));
    usize total = 0;
    while (total < data.size()) {
        const usize rem = data.size() - total;
        const DWORD chunk = (rem > 0x7FFFFFFFu) ? 0x7FFFFFFFu : static_cast<DWORD>(rem);
        OVERLAPPED ov{};
        const u64 uoff = static_cast<u64>(offset) + static_cast<u64>(total);
        ov.Offset = static_cast<DWORD>(uoff & 0xFFFFFFFFULL);
        ov.OffsetHigh = static_cast<DWORD>(uoff >> 32);
        DWORD written = 0;
        if (!WriteFile(h, data.data() + total, chunk, &written, &ov)) {
            return detail::win32_error_to_keel(GetLastError());
        }
        if (written == 0) return Error{ErrorCode::IoError, 0};
        total += as<usize>(written);
    }
    return total;
#else
    const int fd = as<int>(m_handle);
    usize total = 0;
    while (total < data.size()) {
        const ssize_t n =
            ::pwrite(fd, data.data() + total, data.size() - total, static_cast<off_t>(offset + as<i64>(total)));
        if (n < 0) {
            if (errno == EINTR) continue;
            return detail::errno_to_keel(errno);
        }
        if (n == 0) return Error{ErrorCode::IoError, 0};
        total += as<usize>(n);
    }
    return total;
#endif
}

namespace detail::io_lock_impl {

inline Error do_file_lock(i64 handle, FileLockMode mode, bool non_blocking) noexcept {
#if defined(__EMSCRIPTEN__)
    (void)handle;
    (void)mode;
    (void)non_blocking;
    return Error{ErrorCode::NotSupported, 0};
#elif defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<isize>(handle));
    DWORD flags = 0;
    if (mode == FileLockMode::Exclusive) flags |= LOCKFILE_EXCLUSIVE_LOCK;
    if (non_blocking) flags |= LOCKFILE_FAIL_IMMEDIATELY;
    OVERLAPPED ov{};
    if (!LockFileEx(h, flags, 0, MAXDWORD, MAXDWORD, &ov)) {
        const DWORD e = GetLastError();
        if (non_blocking && e == ERROR_LOCK_VIOLATION) {
            return Error{ErrorCode::WouldBlock, as<i32>(e)};
        }
        return detail::win32_error_to_keel(e);
    }
    return Error::ok();
#else
    const int fd = as<int>(handle);
    int op = (mode == FileLockMode::Exclusive) ? LOCK_EX : LOCK_SH;
    if (non_blocking) op |= LOCK_NB;
    for (;;) {
        if (::flock(fd, op) == 0) return Error::ok();
        if (errno == EINTR) continue;
        return detail::errno_to_keel(errno);
    }
#endif
}

} // namespace detail::io_lock_impl

Error File::lock(FileLockMode mode) noexcept {
    if (!is_open()) return Error{ErrorCode::InvalidArgument, 0};
    return detail::io_lock_impl::do_file_lock(m_handle, mode, false);
}

Error File::try_lock(FileLockMode mode) noexcept {
    if (!is_open()) return Error{ErrorCode::InvalidArgument, 0};
    return detail::io_lock_impl::do_file_lock(m_handle, mode, true);
}

Error File::unlock() noexcept {
    if (!is_open()) return Error{ErrorCode::InvalidArgument, 0};
#if defined(__EMSCRIPTEN__)
    return Error{ErrorCode::NotSupported, 0};
#elif defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<isize>(m_handle));
    OVERLAPPED ov{};
    if (!UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ov)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return Error::ok();
#else
    const int fd = as<int>(m_handle);
    for (;;) {
        if (::flock(fd, LOCK_UN) == 0) return Error::ok();
        if (errno == EINTR) continue;
        return detail::errno_to_keel(errno);
    }
#endif
}

Result<usize> stdin_read(MutArrayRef<u8> buf) noexcept {
    if (buf.empty()) return usize{0};
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE) return Error{ErrorCode::IoError, 0};
    const DWORD to_read = (buf.size() > 0x7FFFFFFFu) ? 0x7FFFFFFFu : static_cast<DWORD>(buf.size());
    DWORD nread = 0;
    if (!ReadFile(h, buf.data(), to_read, &nread, nullptr)) {
        const DWORD e = GetLastError();
        if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF) return usize{0};
        return detail::win32_error_to_keel(e);
    }
    return as<usize>(nread);
#else
    for (;;) {
        const ssize_t n = ::read(STDIN_FILENO, buf.data(), buf.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            return detail::errno_to_keel(errno);
        }
        return as<usize>(n);
    }
#endif
}

bool is_tty(StdStream stream) noexcept {
#if defined(__EMSCRIPTEN__)
    (void)stream;
    return false;
#elif defined(_WIN32)
    DWORD id = STD_OUTPUT_HANDLE;
    if (stream == StdStream::Stdin) id = STD_INPUT_HANDLE;
    else if (stream == StdStream::Stderr) id = STD_ERROR_HANDLE;
    HANDLE h = GetStdHandle(id);
    if (!h || h == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
#else
    int fd = STDOUT_FILENO;
    if (stream == StdStream::Stdin) fd = STDIN_FILENO;
    else if (stream == StdStream::Stderr) fd = STDERR_FILENO;
    return ::isatty(fd) != 0;
#endif
}

Optional<TerminalSize> terminal_size(StdStream stream) noexcept {
#if defined(__EMSCRIPTEN__)
    (void)stream;
    return kNullOpt;
#elif defined(_WIN32)
    DWORD id = STD_OUTPUT_HANDLE;
    if (stream == StdStream::Stdin) id = STD_INPUT_HANDLE;
    else if (stream == StdStream::Stderr) id = STD_ERROR_HANDLE;
    HANDLE h = GetStdHandle(id);
    if (!h || h == INVALID_HANDLE_VALUE) return kNullOpt;
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (!GetConsoleScreenBufferInfo(h, &csbi)) return kNullOpt;
    const i32 cols = as<i32>(csbi.srWindow.Right) - as<i32>(csbi.srWindow.Left) + 1;
    const i32 rows = as<i32>(csbi.srWindow.Bottom) - as<i32>(csbi.srWindow.Top) + 1;
    if (cols <= 0 || rows <= 0) return kNullOpt;
    return TerminalSize{as<u32>(cols), as<u32>(rows)};
#else
    int fd = STDOUT_FILENO;
    if (stream == StdStream::Stdin) fd = STDIN_FILENO;
    else if (stream == StdStream::Stderr) fd = STDERR_FILENO;
    struct winsize ws{};
    if (::ioctl(fd, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 || ws.ws_row == 0) {
        return kNullOpt;
    }
    return TerminalSize{as<u32>(ws.ws_col), as<u32>(ws.ws_row)};
#endif
}

bool terminal_enable_ansi() noexcept {
#if defined(__EMSCRIPTEN__)
    return false;
#elif defined(_WIN32)
    (void)SetConsoleOutputCP(CP_UTF8);
    (void)SetConsoleCP(CP_UTF8);
    static constexpr DWORD kEnableVtProcessing = 0x0004u;
    bool ok_any = false;
    const DWORD handles[2] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (usize i = 0; i < 2; ++i) {
        HANDLE h = GetStdHandle(handles[i]);
        if (!h || h == INVALID_HANDLE_VALUE) continue;
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode)) {
            if (SetConsoleMode(h, mode | kEnableVtProcessing)) {
                ok_any = true;
            }
        }
    }
    return ok_any;
#else
    return is_tty(StdStream::Stdout) || is_tty(StdStream::Stderr);
#endif
}

} // namespace keel
