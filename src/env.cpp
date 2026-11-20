#include "keel/env.h"
#include "keel/small_array.h"
#include "keel/utf8.h"

#if defined(_WIN32)
#include "win32_compat.h"
#else
#include <cerrno>
#include <cstdlib>
#endif

namespace keel {

namespace detail {
#if defined(_WIN32)
Error win32_error_to_keel(DWORD err) noexcept;

static bool env_utf8_to_wide(StringRef utf8, SmallArray<wchar_t, 256>& out) {
    out.clear();
    if (utf8.empty()) {
        out.push_back(L'\0');
        return true;
    }
    if (!utf8_validate(utf8)) return false;
    const int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) return false;
    out.resize(static_cast<usize>(wlen) + 1);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                        out.data(), wlen);
    out[static_cast<usize>(wlen)] = L'\0';
    return true;
}

static bool env_wide_to_utf8(const wchar_t* wstr, usize wlen, String& out) {
    out.clear();
    if (!wstr || wlen == 0) return true;
    const int u8len =
        WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(wlen), nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return false;
    out.resize(static_cast<usize>(u8len));
    WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(wlen), out.data(), u8len, nullptr, nullptr);
    return true;
}
#else
Error errno_to_keel(int err) noexcept;
#endif

static bool is_valid_env_name(StringRef name) noexcept {
    if (name.empty()) return false;
    for (usize i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '=' || c == '\0') return false;
    }
    return true;
}

static bool is_valid_env_value(StringRef value) noexcept {
    for (usize i = 0; i < value.size(); ++i) {
        if (value[i] == '\0') return false;
    }
    return true;
}
} // namespace detail

Optional<String> env_get(StringRef name, Allocator* alloc) {
    if (!detail::is_valid_env_name(name)) return kNullOpt;
    Allocator* a = alloc ? alloc : default_allocator();

#if defined(_WIN32)
    SmallArray<wchar_t, 256> wname;
    if (!detail::env_utf8_to_wide(name, wname)) return kNullOpt;

    SetLastError(ERROR_SUCCESS);
    DWORD needed = GetEnvironmentVariableW(wname.data(), nullptr, 0);
    if (needed == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return kNullOpt;
        return String(a); // Explicitly set to empty string
    }

    SmallArray<wchar_t, 512> wbuf;
    wbuf.resize(static_cast<usize>(needed));
    SetLastError(ERROR_SUCCESS);
    DWORD written = GetEnvironmentVariableW(wname.data(), wbuf.data(), needed);
    if (written == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return kNullOpt;
        return String(a);
    }
    // Retry once if the variable grew between calls
    if (written >= needed) {
        wbuf.resize(static_cast<usize>(written) + 1);
        written = GetEnvironmentVariableW(wname.data(), wbuf.data(), static_cast<DWORD>(wbuf.size()));
    }

    String result(a);
    detail::env_wide_to_utf8(wbuf.data(), static_cast<usize>(written), result);
    return result;
#else
    SmallString<128> key(name);
    const char* val = ::getenv(key.c_str());
    if (!val) return kNullOpt;
    return String(val, a);
#endif
}

String env_get_or_empty(StringRef name, Allocator* alloc) {
    Allocator* a = alloc ? alloc : default_allocator();
    Optional<String> val = env_get(name, a);
    if (val.has_value()) return keel::move(*val);
    return String(a);
}

bool env_has(StringRef name) noexcept {
    if (!detail::is_valid_env_name(name)) return false;

#if defined(_WIN32)
    SmallArray<wchar_t, 256> wname;
    if (!detail::env_utf8_to_wide(name, wname)) return false;
    SetLastError(ERROR_SUCCESS);
    const DWORD needed = GetEnvironmentVariableW(wname.data(), nullptr, 0);
    if (needed > 0) return true;
    return GetLastError() != ERROR_ENVVAR_NOT_FOUND;
#else
    SmallString<128> key(name);
    return ::getenv(key.c_str()) != nullptr;
#endif
}

Error env_set(StringRef name, StringRef value) noexcept {
    if (!detail::is_valid_env_name(name) || !detail::is_valid_env_value(value)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }

#if defined(_WIN32)
    SmallArray<wchar_t, 256> wname;
    SmallArray<wchar_t, 512> wval;
    if (!detail::env_utf8_to_wide(name, wname) || !detail::env_utf8_to_wide(value, wval)) {
        return Error{ErrorCode::InvalidData, 0};
    }
    if (!SetEnvironmentVariableW(wname.data(), wval.data())) {
        return detail::win32_error_to_keel(GetLastError());
    }
    return Error::ok();
#else
    SmallString<128> key(name);
    SmallString<256> val(value);
    if (::setenv(key.c_str(), val.c_str(), 1) != 0) {
        return detail::errno_to_keel(errno);
    }
    return Error::ok();
#endif
}

Error env_unset(StringRef name) noexcept {
    if (!detail::is_valid_env_name(name)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }

#if defined(_WIN32)
    SmallArray<wchar_t, 256> wname;
    if (!detail::env_utf8_to_wide(name, wname)) {
        return Error{ErrorCode::InvalidData, 0};
    }
    if (!SetEnvironmentVariableW(wname.data(), nullptr)) {
        const DWORD err = GetLastError();
        if (err == ERROR_ENVVAR_NOT_FOUND) return Error::ok();
        return detail::win32_error_to_keel(err);
    }
    return Error::ok();
#else
    SmallString<128> key(name);
    if (::unsetenv(key.c_str()) != 0) {
        return detail::errno_to_keel(errno);
    }
    return Error::ok();
#endif
}

} // namespace keel
