#pragma once

#include "keel/core.h"
#include "keel/error.h"
#include "keel/ref.h"
#include "keel/result.h"
#include "keel/string.h"

namespace keel {

enum class DynLibFlags : u32 {
    None   = 0,
    Local  = 0,        // Default: keep symbols private to the library (RTLD_LOCAL on POSIX)
    Global = 1u << 0,  // Export symbols globally for later loads (RTLD_GLOBAL on POSIX)
    Lazy   = 1u << 1,  // Lazy symbol resolution when supported (RTLD_LAZY on POSIX; default RTLD_NOW)
};

[[nodiscard]] constexpr DynLibFlags operator|(DynLibFlags a, DynLibFlags b) noexcept {
    return from_underlying<DynLibFlags>(to_underlying(a) | to_underlying(b));
}

[[nodiscard]] constexpr DynLibFlags operator&(DynLibFlags a, DynLibFlags b) noexcept {
    return from_underlying<DynLibFlags>(to_underlying(a) & to_underlying(b));
}

[[nodiscard]] constexpr bool has_flag(DynLibFlags flags, DynLibFlags bit) noexcept {
    return (to_underlying(flags) & to_underlying(bit)) != 0;
}

// Platform default shared library filename extension (".so", ".dylib", ".dll") and prefix ("lib", "").
[[nodiscard]] constexpr StringRef dynlib_extension() noexcept {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

[[nodiscard]] constexpr StringRef dynlib_prefix() noexcept {
#if defined(_WIN32)
    return "";
#else
    return "lib";
#endif
}

// Resolves the filesystem path of the shared library or executable containing `addr`.
[[nodiscard]] Result<String> dynlib_path_from_address(const void* addr, Allocator* alloc = nullptr);

// Move-only RAII handle for an OS dynamic library (`dlopen` on POSIX, `LoadLibraryW` on Win32).
class DynLib {
public:
    constexpr DynLib() noexcept = default;
    ~DynLib() noexcept { close(); }

    DynLib(const DynLib&) = delete;
    DynLib& operator=(const DynLib&) = delete;

    constexpr DynLib(DynLib&& other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }

    DynLib& operator=(DynLib&& other) noexcept {
        if (this != &other) {
            close();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    // Loads a shared library from UTF-8 `path`. If `err_msg` is non-null, writes OS diagnostic text on failure.
    [[nodiscard]] static Result<DynLib> open(StringRef path,
                                             DynLibFlags flags = DynLibFlags::Local,
                                             String* err_msg = nullptr) noexcept;

    // Resolves a symbol by name (`dlsym` / `GetProcAddress`). Returns nullptr if not found or closed.
    [[nodiscard]] void* symbol_raw(StringRef name) const noexcept;

    // Typed symbol lookup helper (strict-aliasing & `-Wpedantic` safe for function and object pointers).
    template <typename T>
    [[nodiscard]] T symbol(StringRef name) const noexcept {
        static_assert(sizeof(T) == sizeof(void*), "DynLib::symbol<T> requires pointer-sized T");
        void* p = symbol_raw(name);
        T out = nullptr;
        if (p) mem_copy(&out, &p, sizeof(out));
        return out;
    }

    void close() noexcept;

    // Detaches and returns the raw OS handle without closing it (e.g., to pin a loaded plugin for process lifetime).
    [[nodiscard]] void* release() noexcept {
        void* h = m_handle;
        m_handle = nullptr;
        return h;
    }

    [[nodiscard]] constexpr bool is_open() const noexcept { return m_handle != nullptr; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_open(); }
    [[nodiscard]] constexpr void* native_handle() const noexcept { return m_handle; }

private:
    explicit constexpr DynLib(void* h) noexcept : m_handle(h) {}
    void* m_handle = nullptr;
};

} // namespace keel
