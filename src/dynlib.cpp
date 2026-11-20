#include "keel/dynlib.h"
#include "keel/arena.h"
#include "keel/fs.h"
#include "keel/small_array.h"

#if defined(__EMSCRIPTEN__)

namespace keel {

Result<String> dynlib_path_from_address(const void* /*addr*/, Allocator* /*alloc*/) {
    return Error{ErrorCode::NotSupported, 0};
}

Result<DynLib> DynLib::open(StringRef /*path*/, DynLibFlags /*flags*/, String* err_msg) noexcept {
    if (err_msg) err_msg->assign("DynLib is not supported on WebAssembly/Emscripten");
    return Error{ErrorCode::NotSupported, 0};
}

void* DynLib::symbol_raw(StringRef /*name*/) const noexcept { return nullptr; }

void DynLib::close() noexcept { m_handle = nullptr; }

} // namespace keel

#elif defined(_WIN32)

#include "win32_compat.h"

namespace keel {

namespace detail {
Error win32_error_to_keel(DWORD err) noexcept;
bool utf8_path_to_win32_wide(StringRef utf8_path, SmallArray<wchar_t, 260>& out);
void win32_wide_to_utf8_path(const wchar_t* wstr, usize wlen, String& out);

static void format_win32_error_utf8(DWORD err, String& out) {
    out.clear();
    wchar_t wbuf[512];
    const DWORD wlen = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      wbuf, static_cast<DWORD>(sizeof(wbuf) / sizeof(wbuf[0])), nullptr);
    if (wlen == 0) {
        out.assign("LoadLibraryW failed");
        return;
    }
    usize trimmed = static_cast<usize>(wlen);
    while (trimmed > 0 && (wbuf[trimmed - 1] == L'\r' || wbuf[trimmed - 1] == L'\n')) {
        --trimmed;
    }
    if (trimmed == 0) return;
    const int u8len =
        WideCharToMultiByte(CP_UTF8, 0, wbuf, static_cast<int>(trimmed), nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return;
    out.resize(static_cast<usize>(u8len));
    WideCharToMultiByte(CP_UTF8, 0, wbuf, static_cast<int>(trimmed), out.data(), u8len, nullptr, nullptr);
}
} // namespace detail

Result<DynLib> DynLib::open(StringRef path, DynLibFlags /*flags*/, String* err_msg) noexcept {
    if (path.empty()) {
        if (err_msg) err_msg->assign("DynLib::open: empty library path");
        return Error{ErrorCode::InvalidArgument, 0};
    }

    SmallArray<wchar_t, 260> wpath;
    if (!detail::utf8_path_to_win32_wide(path, wpath)) {
        if (err_msg) err_msg->assign("DynLib::open: invalid UTF-8 path");
        return Error{ErrorCode::InvalidData, 0};
    }

    HMODULE h = LoadLibraryW(wpath.data());
    if (!h) {
        const DWORD err = GetLastError();
        if (err_msg) detail::format_win32_error_utf8(err, *err_msg);
        return detail::win32_error_to_keel(err);
    }
    return DynLib(static_cast<void*>(h));
}

void* DynLib::symbol_raw(StringRef name) const noexcept {
    if (!m_handle || name.empty()) return nullptr;
    SmallString<128> sym(name);
    FARPROC proc = GetProcAddress(static_cast<HMODULE>(m_handle), sym.c_str());
    void* out = nullptr;
    if (proc) mem_copy(&out, &proc, sizeof(out));
    return out;
}

void DynLib::close() noexcept {
    if (m_handle) {
        FreeLibrary(static_cast<HMODULE>(m_handle));
        m_handle = nullptr;
    }
}

Result<String> dynlib_path_from_address(const void* addr, Allocator* alloc) {
    if (!addr) return Error{ErrorCode::InvalidArgument, 0};
    HMODULE hm = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &hm) ||
        !hm) {
        return detail::win32_error_to_keel(GetLastError());
    }
    ArenaTemp scratch = get_scratch(alloc);
    SmallArray<wchar_t, 260> wbuf(scratch.allocator());
    DWORD cap = 260;
    for (;;) {
        wbuf.reserve(static_cast<usize>(cap));
        const DWORD len = GetModuleFileNameW(hm, wbuf.data(), cap);
        if (len == 0) return detail::win32_error_to_keel(GetLastError());
        if (len < cap) {
            String out(alloc);
            detail::win32_wide_to_utf8_path(wbuf.data(), static_cast<usize>(len), out);
            return out;
        }
        cap *= 2;
    }
}

} // namespace keel

#else // POSIX

#include <dlfcn.h>

namespace keel {

Result<DynLib> DynLib::open(StringRef path, DynLibFlags flags, String* err_msg) noexcept {
    if (path.empty()) {
        if (err_msg) err_msg->assign("DynLib::open: empty library path");
        return Error{ErrorCode::InvalidArgument, 0};
    }

    SmallString<256> cpath(path);
    int mode = has_flag(flags, DynLibFlags::Lazy) ? RTLD_LAZY : RTLD_NOW;
    mode |= has_flag(flags, DynLibFlags::Global) ? RTLD_GLOBAL : RTLD_LOCAL;

    (void)::dlerror(); // Clear any prior error on this thread
    void* h = ::dlopen(cpath.c_str(), mode);
    if (!h) {
        const char* dl_err = ::dlerror();
        if (err_msg) {
            err_msg->assign(dl_err ? StringRef(dl_err) : StringRef("dlopen failed"));
        }
        return Error{ErrorCode::NotFound, 0};
    }
    return DynLib(h);
}

void* DynLib::symbol_raw(StringRef name) const noexcept {
    if (!m_handle || name.empty()) return nullptr;
    SmallString<128> sym(name);
    return ::dlsym(m_handle, sym.c_str());
}

void DynLib::close() noexcept {
    if (m_handle) {
        ::dlclose(m_handle);
        m_handle = nullptr;
    }
}

Result<String> dynlib_path_from_address(const void* addr, Allocator* alloc) {
    if (!addr) return Error{ErrorCode::InvalidArgument, 0};
    Dl_info info{};
    if (::dladdr(addr, &info) == 0 || !info.dli_fname || info.dli_fname[0] == '\0') {
        return Error{ErrorCode::NotFound, 0};
    }
    const StringRef raw(info.dli_fname);
    if (auto real = fs_realpath(raw, alloc); real.is_ok()) {
        return keel::move(*real);
    }
    return path_normalize(raw, alloc);
}

} // namespace keel

#endif
