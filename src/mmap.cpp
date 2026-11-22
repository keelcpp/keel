#include "keel/mmap.h"
#include "keel/arena.h"
#include "keel/small_array.h"

#if defined(_WIN32)
#include "win32_compat.h"
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace keel {

namespace detail {
#if defined(_WIN32)
Error win32_error_to_keel(DWORD err) noexcept;
bool utf8_path_to_win32_wide(StringRef utf8_path, SmallArray<wchar_t, 260>& out);
#else
Error errno_to_keel(int err) noexcept;
#endif
} // namespace detail

#if defined(_WIN32)

Result<MappedFile> MappedFile::open(StringRef path, MapMode mode) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};

    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }

    const DWORD desired = (mode == MapMode::ReadWrite) ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    const DWORD share =
        (mode == MapMode::ReadWrite) ? FILE_SHARE_READ : (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    HANDLE fh = CreateFileW(wpath.data(), desired, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        return detail::win32_error_to_keel(GetLastError());
    }

    LARGE_INTEGER li{};
    if (!GetFileSizeEx(fh, &li) || li.QuadPart < 0) {
        const Error err = detail::win32_error_to_keel(GetLastError());
        CloseHandle(fh);
        return err;
    }
    const u64 fsize = static_cast<u64>(li.QuadPart);
    if (fsize > as<u64>(Limits<usize>::kMax)) {
        CloseHandle(fh);
        return Error{ErrorCode::Overflow, 0};
    }

    MappedFile out;
    out.m_mode = mode;
    out.m_open = true;
    if (fsize == 0) {
        CloseHandle(fh);
        return out;
    }

    DWORD protect = PAGE_READONLY;
    DWORD access = FILE_MAP_READ;
    if (mode == MapMode::ReadWrite) {
        protect = PAGE_READWRITE;
        access = FILE_MAP_WRITE;
    } else if (mode == MapMode::CopyOnWrite) {
        protect = PAGE_WRITECOPY;
        access = FILE_MAP_COPY;
    }

    HANDLE mh = CreateFileMappingW(fh, nullptr, protect, 0, 0, nullptr);
    if (!mh) {
        const Error err = detail::win32_error_to_keel(GetLastError());
        CloseHandle(fh);
        return err;
    }

    void* view = MapViewOfFile(mh, access, 0, 0, static_cast<SIZE_T>(fsize));
    if (!view) {
        const Error err = detail::win32_error_to_keel(GetLastError());
        CloseHandle(mh);
        CloseHandle(fh);
        return err;
    }

    out.m_data = static_cast<u8*>(view);
    out.m_size = static_cast<usize>(fsize);
    // Win32 mappings remain valid after closing the mapping handle; keep the file handle only for ReadWrite flushes.
    CloseHandle(mh);
    if (mode == MapMode::ReadWrite) {
        out.m_file_handle = static_cast<void*>(fh);
    } else {
        CloseHandle(fh);
    }
    return out;
}

Result<MappedFile> MappedFile::open_create(StringRef path, usize size) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
    if (static_cast<u64>(size) > 0x7FFFFFFFFFFFFFFFULL) {
        return Error{ErrorCode::Overflow, 0};
    }

    ArenaTemp scratch = get_scratch();
    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(path, wpath)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }

    HANDLE fh = CreateFileW(
        wpath.data(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (fh == INVALID_HANDLE_VALUE) {
        return detail::win32_error_to_keel(GetLastError());
    }

    MappedFile out;
    out.m_mode = MapMode::ReadWrite;
    out.m_open = true;
    if (size == 0) {
        CloseHandle(fh);
        return out;
    }

    LARGE_INTEGER li{};
    li.QuadPart = static_cast<LONGLONG>(size);
    if (!SetFilePointerEx(fh, li, nullptr, FILE_BEGIN) || !SetEndOfFile(fh)) {
        const Error err = detail::win32_error_to_keel(GetLastError());
        CloseHandle(fh);
        return err;
    }

    HANDLE mh = CreateFileMappingW(fh, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (!mh) {
        const Error err = detail::win32_error_to_keel(GetLastError());
        CloseHandle(fh);
        return err;
    }

    void* view = MapViewOfFile(mh, FILE_MAP_WRITE, 0, 0, static_cast<SIZE_T>(size));
    if (!view) {
        const Error err = detail::win32_error_to_keel(GetLastError());
        CloseHandle(mh);
        CloseHandle(fh);
        return err;
    }

    CloseHandle(mh);
    out.m_data = static_cast<u8*>(view);
    out.m_size = size;
    out.m_file_handle = static_cast<void*>(fh);
    return out;
}

Error MappedFile::flush() noexcept {
    if (!m_open || m_mode != MapMode::ReadWrite || !m_data || m_size == 0) {
        return Error::ok();
    }
    if (!FlushViewOfFile(m_data, m_size)) {
        return detail::win32_error_to_keel(GetLastError());
    }
    if (m_file_handle) {
        if (!FlushFileBuffers(static_cast<HANDLE>(m_file_handle))) {
            return detail::win32_error_to_keel(GetLastError());
        }
    }
    return Error::ok();
}

void MappedFile::close() noexcept {
    if (m_data) {
        UnmapViewOfFile(m_data);
        m_data = nullptr;
    }
    if (m_map_handle) {
        CloseHandle(static_cast<HANDLE>(m_map_handle));
        m_map_handle = nullptr;
    }
    if (m_file_handle) {
        CloseHandle(static_cast<HANDLE>(m_file_handle));
        m_file_handle = nullptr;
    }
    m_size = 0;
    m_open = false;
}

#else // POSIX (Linux, macOS, BSD, Emscripten)

Result<MappedFile> MappedFile::open(StringRef path, MapMode mode) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};

    ArenaTemp scratch = get_scratch();
    const String cpath = String::from(path, scratch.allocator());
    int flags = (mode == MapMode::ReadWrite) ? O_RDWR : O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
    const int fd = ::open(cpath.c_str(), flags);
    if (fd < 0) {
        return detail::errno_to_keel(errno);
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        const Error err = detail::errno_to_keel(errno);
        (void)::close(fd);
        return err;
    }
    if (!S_ISREG(st.st_mode)) {
        (void)::close(fd);
        return Error{ErrorCode::InvalidArgument, EINVAL};
    }
    if (st.st_size < 0 || static_cast<u64>(st.st_size) > as<u64>(Limits<usize>::kMax)) {
        (void)::close(fd);
        return Error{ErrorCode::Overflow, 0};
    }

    const usize fsize = static_cast<usize>(st.st_size);
    MappedFile out;
    out.m_mode = mode;
    out.m_open = true;
    if (fsize == 0) {
        (void)::close(fd);
        return out;
    }

    const int prot = (mode == MapMode::ReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE);
    const int map_flags = (mode == MapMode::CopyOnWrite) ? MAP_PRIVATE : MAP_SHARED;
    void* view = ::mmap(nullptr, fsize, prot, map_flags, fd, 0);
    const int map_errno = errno;
    // POSIX mappings hold their own kernel reference; close fd immediately so we never consume process FD slots.
    (void)::close(fd);
    if (view == MAP_FAILED) {
        return detail::errno_to_keel(map_errno);
    }

    out.m_data = static_cast<u8*>(view);
    out.m_size = fsize;
    return out;
}

Result<MappedFile> MappedFile::open_create(StringRef path, usize size) noexcept {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, 0};
    if (static_cast<u64>(size) > 0x7FFFFFFFFFFFFFFFULL) {
        return Error{ErrorCode::Overflow, 0};
    }

    ArenaTemp scratch = get_scratch();
    const String cpath = String::from(path, scratch.allocator());
    int flags = O_RDWR | O_CREAT | O_TRUNC;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
    const int fd = ::open(cpath.c_str(), flags, static_cast<mode_t>(0666));
    if (fd < 0) {
        return detail::errno_to_keel(errno);
    }

    MappedFile out;
    out.m_mode = MapMode::ReadWrite;
    out.m_open = true;
    if (size == 0) {
        (void)::close(fd);
        return out;
    }

    if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        const Error err = detail::errno_to_keel(errno);
        (void)::close(fd);
        return err;
    }

    void* view = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    const int map_errno = errno;
    (void)::close(fd);
    if (view == MAP_FAILED) {
        return detail::errno_to_keel(map_errno);
    }

    out.m_data = static_cast<u8*>(view);
    out.m_size = size;
    return out;
}

Error MappedFile::flush() noexcept {
    if (!m_open || m_mode != MapMode::ReadWrite || !m_data || m_size == 0) {
        return Error::ok();
    }
#if !defined(__EMSCRIPTEN__)
    if (::msync(m_data, m_size, MS_SYNC) != 0) {
        return detail::errno_to_keel(errno);
    }
#endif
    return Error::ok();
}

void MappedFile::close() noexcept {
    if (m_data && m_size > 0) {
        (void)::munmap(m_data, m_size);
        m_data = nullptr;
    }
    m_file_handle = nullptr;
    m_map_handle = nullptr;
    m_size = 0;
    m_open = false;
}

#endif

} // namespace keel
