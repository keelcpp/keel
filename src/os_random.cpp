#include "keel/os_random.h"
#include "keel/memory.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#elif defined(_WIN32)
#include "win32_compat.h"
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <stdlib.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

namespace keel {

namespace detail {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__) && !defined(__FreeBSD__) && \
    !defined(__OpenBSD__) && !defined(__NetBSD__)
Error errno_to_keel(int err) noexcept;
#endif
} // namespace detail

#if defined(__EMSCRIPTEN__)

namespace detail::random_impl {

EM_JS(i32, keel_js_get_random_values, (u8* ptr, usize len), {
    var cryptoObj = (typeof globalThis !== 'undefined' && globalThis.crypto)
        ? globalThis.crypto
        : (typeof crypto !== 'undefined' ? crypto : null);
    if (!cryptoObj || typeof cryptoObj.getRandomValues !== 'function') return 0;
    var offset = 0;
    while (offset < len) {
        var chunk = Math.min(len - offset, 65536);
        cryptoObj.getRandomValues(HEAPU8.subarray(ptr + offset, ptr + offset + chunk));
        offset += chunk;
    }
    return 1;
});

} // namespace detail::random_impl

Error os_random_bytes(MutArrayRef<u8> out) noexcept {
    if (out.empty()) return Error::ok();
    if (detail::random_impl::keel_js_get_random_values(out.data(), out.size()) == 0) {
        return Error{ErrorCode::NotSupported, 0};
    }
    return Error::ok();
}

#elif defined(_WIN32)

namespace detail::random_impl {

using PfnBCryptGenRandom = LONG(WINAPI*)(void*, u8*, ULONG, ULONG);
using PfnRtlGenRandom = BOOLEAN(WINAPI*)(void*, ULONG);

struct Win32RngProcs {
    PfnBCryptGenRandom bcrypt_gen = nullptr;
    PfnRtlGenRandom rtl_gen = nullptr;

    Win32RngProcs() noexcept {
        // Resolve once and keep the OS module pinned for process lifetime to avoid per-call LoadLibrary/FreeLibrary.
        if (HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll")) {
            bcrypt_gen = reinterpret_cast<PfnBCryptGenRandom>(reinterpret_cast<void*>(GetProcAddress(bcrypt, "BCryptGenRandom")));
        }
        if (!bcrypt_gen) {
            if (HMODULE advapi = LoadLibraryW(L"advapi32.dll")) {
                rtl_gen = reinterpret_cast<PfnRtlGenRandom>(reinterpret_cast<void*>(GetProcAddress(advapi, "SystemFunction036")));
            }
        }
    }
};

inline const Win32RngProcs& get_win32_rng() noexcept {
    static const Win32RngProcs s_procs;
    return s_procs;
}

} // namespace detail::random_impl

Error os_random_bytes(MutArrayRef<u8> out) noexcept {
    if (out.empty()) return Error::ok();

    static constexpr ULONG kBcryptUseSystemPreferredRng = 0x00000002u;
    const auto& procs = detail::random_impl::get_win32_rng();

    if (procs.bcrypt_gen) {
        usize off = 0;
        while (off < out.size()) {
            const usize rem = out.size() - off;
            const ULONG chunk = (rem > 0x40000000u) ? 0x40000000u : as<ULONG>(rem);
            const LONG st = procs.bcrypt_gen(nullptr, out.data() + off, chunk, kBcryptUseSystemPreferredRng);
            if (st < 0) {
                return Error{ErrorCode::IoError, static_cast<i32>(st)};
            }
            off += as<usize>(chunk);
        }
        return Error::ok();
    }

    if (procs.rtl_gen) {
        usize off = 0;
        while (off < out.size()) {
            const usize rem = out.size() - off;
            const ULONG chunk = (rem > 0x40000000u) ? 0x40000000u : as<ULONG>(rem);
            if (!procs.rtl_gen(out.data() + off, chunk)) {
                return Error{ErrorCode::IoError, 0};
            }
            off += as<usize>(chunk);
        }
        return Error::ok();
    }

    return Error{ErrorCode::NotSupported, 0};
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

Error os_random_bytes(MutArrayRef<u8> out) noexcept {
    if (out.empty()) return Error::ok();
    ::arc4random_buf(out.data(), out.size());
    return Error::ok();
}

#else // Linux / generic POSIX

namespace detail::random_impl {

inline Error read_urandom(MutArrayRef<u8> out) noexcept {
    const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return detail::errno_to_keel(errno);
    }
    usize off = 0;
    while (off < out.size()) {
        const ssize_t n = ::read(fd, out.data() + off, out.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            const Error err = detail::errno_to_keel(errno);
            (void)::close(fd);
            return err;
        }
        if (n == 0) {
            (void)::close(fd);
            return Error{ErrorCode::IoError, 0};
        }
        off += as<usize>(n);
    }
    (void)::close(fd);
    return Error::ok();
}

} // namespace detail::random_impl

Error os_random_bytes(MutArrayRef<u8> out) noexcept {
    if (out.empty()) return Error::ok();

#if defined(__linux__)
    usize off = 0;
    while (off < out.size()) {
        const ssize_t n = ::getrandom(out.data() + off, out.size() - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == ENOSYS) {
                return detail::random_impl::read_urandom(out.slice(off));
            }
            return detail::errno_to_keel(errno);
        }
        if (n == 0) {
            return Error{ErrorCode::IoError, 0};
        }
        off += as<usize>(n);
    }
    return Error::ok();
#else
    return detail::random_impl::read_urandom(out);
#endif
}

#endif

Result<u64> os_random_u64() noexcept {
    u8 buf[8]{};
    const Error err = os_random_bytes(MutArrayRef<u8>(buf, sizeof(buf)));
    if (err.is_err()) return err;
    u64 val = 0;
    mem_copy(&val, buf, sizeof(val));
    return val;
}

Result<u32> os_random_u32() noexcept {
    u8 buf[4]{};
    const Error err = os_random_bytes(MutArrayRef<u8>(buf, sizeof(buf)));
    if (err.is_err()) return err;
    u32 val = 0;
    mem_copy(&val, buf, sizeof(val));
    return val;
}

} // namespace keel
