#include "keel/io.h"
#include "keel/arena.h"
#include "keel/small_array.h"
#include "keel/utf8.h"

#include <cstdio>

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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace keel {

namespace detail {

#if defined(_WIN32)
Error win32_error_to_keel(DWORD err) noexcept {
    switch (err) {
    case ERROR_SUCCESS:
        return Error::ok();
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return Error{ErrorCode::NotFound, static_cast<i32>(err)};
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return Error{ErrorCode::PermissionDenied, static_cast<i32>(err)};
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return Error{ErrorCode::AlreadyExists, static_cast<i32>(err)};
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
        return Error{ErrorCode::InvalidArgument, static_cast<i32>(err)};
    case ERROR_HANDLE_EOF:
        return Error{ErrorCode::EndOfFile, static_cast<i32>(err)};
    default:
        return Error{ErrorCode::IoError, static_cast<i32>(err)};
    }
}

// Converts UTF-8 path (with `/` or `\`) into null-terminated UTF-16 `wchar_t` buffer in `out`.
bool utf8_path_to_win32_wide(StringRef utf8_path, SmallArray<wchar_t, 260>& out) {
    if (utf8_path.empty() || !utf8_validate(utf8_path)) return false;

    const int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path.data(),
                                         static_cast<int>(utf8_path.size()), nullptr, 0);
    if (wlen <= 0) return false;

    const bool needs_long_prefix = (wlen >= MAX_PATH - 12) && !utf8_path.starts_with("\\\\");
    const usize prefix_len = needs_long_prefix ? 4 : 0;
    out.clear();
    out.reserve(prefix_len + static_cast<usize>(wlen) + 1);
    if (needs_long_prefix) {
        out.push_back(L'\\');
        out.push_back(L'\\');
        out.push_back(L'?');
        out.push_back(L'\\');
    }
    for (int i = 0; i < wlen; ++i) {
        out.push_back(L'\0');
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path.data(), static_cast<int>(utf8_path.size()),
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
        WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(wlen), nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return;
    out.resize(static_cast<usize>(u8len));
    WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(wlen), out.data(), u8len, nullptr, nullptr);
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
    return File(static_cast<i64>(fd));
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
    return static_cast<usize>(bytes_read);
#else
    const int fd = static_cast<int>(m_handle);
    const ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n < 0) {
        return detail::errno_to_keel(errno);
    }
    return static_cast<usize>(n);
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
        rem -= static_cast<usize>(written);
    }
#else
    const int fd = static_cast<int>(m_handle);
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
        rem -= static_cast<usize>(n);
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
    const int fd = static_cast<int>(m_handle);
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
    return static_cast<i64>(li_new.QuadPart);
#else
    const int fd = static_cast<int>(m_handle);
    int whence = SEEK_SET;
    if (origin == SeekOrigin::Current) whence = SEEK_CUR;
    else if (origin == SeekOrigin::End) whence = SEEK_END;
    const off_t pos = ::lseek(fd, static_cast<off_t>(offset), whence);
    if (pos == static_cast<off_t>(-1)) {
        return detail::errno_to_keel(errno);
    }
    return static_cast<i64>(pos);
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
    return static_cast<i64>(pos.QuadPart);
#else
    const int fd = static_cast<int>(m_handle);
    const off_t pos = ::lseek(fd, 0, SEEK_CUR);
    if (pos == static_cast<off_t>(-1)) {
        return detail::errno_to_keel(errno);
    }
    return static_cast<i64>(pos);
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
    return static_cast<i64>(sz.QuadPart);
#else
    const int fd = static_cast<int>(m_handle);
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        return detail::errno_to_keel(errno);
    }
    return static_cast<i64>(st.st_size);
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
    const int fd = static_cast<int>(m_handle);
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

} // namespace keel
