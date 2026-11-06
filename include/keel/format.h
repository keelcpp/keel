#pragma once

#include "keel/arena.h"
#include "keel/number.h"
#include "keel/string.h"

namespace keel {

struct FormatArg {
    enum class Kind : u8 {
        Signed,
        Unsigned,
        Float,
        Double,
        Char,
        CString,
        StringRef,
        Pointer,
        Bool
    };

    Kind kind = Kind::Signed;
    union {
        i64 i_val;
        u64 u_val;
        f64 f_val;
        const char* cstr_val;
        const void* ptr_val;
    };
    isize str_len = 0;

    constexpr FormatArg(bool v) noexcept : kind(Kind::Bool), u_val(v ? 1 : 0) {}
    constexpr FormatArg(char v) noexcept : kind(Kind::Char), i_val(static_cast<unsigned char>(v)) {}
    constexpr FormatArg(signed char v) noexcept : kind(Kind::Signed), i_val(v) {}
    constexpr FormatArg(short v) noexcept : kind(Kind::Signed), i_val(v) {}
    constexpr FormatArg(int v) noexcept : kind(Kind::Signed), i_val(v) {}
    constexpr FormatArg(long v) noexcept : kind(Kind::Signed), i_val(v) {}
    constexpr FormatArg(long long v) noexcept : kind(Kind::Signed), i_val(v) {}

    constexpr FormatArg(unsigned char v) noexcept : kind(Kind::Unsigned), u_val(v) {}
    constexpr FormatArg(unsigned short v) noexcept : kind(Kind::Unsigned), u_val(v) {}
    constexpr FormatArg(unsigned int v) noexcept : kind(Kind::Unsigned), u_val(v) {}
    constexpr FormatArg(unsigned long v) noexcept : kind(Kind::Unsigned), u_val(v) {}
    constexpr FormatArg(unsigned long long v) noexcept : kind(Kind::Unsigned), u_val(v) {}

    constexpr FormatArg(float v) noexcept : kind(Kind::Float), f_val(static_cast<f64>(v)) {}
    constexpr FormatArg(double v) noexcept : kind(Kind::Double), f_val(v) {}

    constexpr FormatArg(const char* s) noexcept : kind(Kind::CString), cstr_val(s ? s : "(null)") {}
    constexpr FormatArg(char* s) noexcept : kind(Kind::CString), cstr_val(s ? s : "(null)") {}
    constexpr FormatArg(StringRef s) noexcept : kind(Kind::StringRef), cstr_val(s.data()), str_len(s.size()) {}
    FormatArg(const String& s) noexcept : kind(Kind::StringRef), cstr_val(s.data()), str_len(s.size()) {}
    template <isize N>
    constexpr FormatArg(const InplaceString<N>& s) noexcept
        : kind(Kind::StringRef), cstr_val(s.data()), str_len(s.size()) {}

    template <typename T>
        requires(!IsSame<RemoveCv<T>, char>)
    constexpr FormatArg(const T* p) noexcept : kind(Kind::Pointer), ptr_val(static_cast<const void*>(p)) {}
    constexpr FormatArg(decltype(nullptr)) noexcept : kind(Kind::Pointer), ptr_val(nullptr) {}
};

// Low-level type-erased printf-like formatter (`%d`, `%u`, `%x`, `%X`, `%f`, `%.2f`, `%g`, `%s`, `%S`, `%c`, `%p`, `%%`)
void vformat_into(String& out, StringRef fmt, ArrayRef<FormatArg> args);
isize vformat_buf(char* buf, isize cap, StringRef fmt, ArrayRef<FormatArg> args) noexcept;
StringRef vformat_arena(Arena& arena, StringRef fmt, ArrayRef<FormatArg> args);

template <typename... Args> void format_into(String& out, StringRef fmt, const Args&... args) {
    if constexpr (sizeof...(Args) == 0) {
        vformat_into(out, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vformat_into(out, fmt, ArrayRef<FormatArg>(arg_array, static_cast<isize>(sizeof...(Args))));
    }
}

template <typename... Args> [[nodiscard]] String format(StringRef fmt, const Args&... args) {
    String out;
    format_into(out, fmt, args...);
    return out;
}

template <typename... Args>
[[nodiscard]] String format_alloc(Allocator* alloc, StringRef fmt, const Args&... args) {
    String out(alloc);
    format_into(out, fmt, args...);
    return out;
}

template <typename... Args> isize format_buf(char* buf, isize cap, StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        return vformat_buf(buf, cap, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        return vformat_buf(buf, cap, fmt, ArrayRef<FormatArg>(arg_array, static_cast<isize>(sizeof...(Args))));
    }
}

template <isize N, typename... Args>
void format_into(InplaceString<N>& out, StringRef fmt, const Args&... args) noexcept {
    const isize len = format_buf(out.inline_buffer(), N, fmt, args...);
    out.set_inline_size(len);
}

template <isize N = 64, typename... Args>
[[nodiscard]] InplaceString<N> format_inplace(StringRef fmt, const Args&... args) noexcept {
    InplaceString<N> out;
    format_into(out, fmt, args...);
    return out;
}

template <typename... Args> [[nodiscard]] StringRef format_arena(Arena& arena, StringRef fmt, const Args&... args) {
    if constexpr (sizeof...(Args) == 0) {
        return vformat_arena(arena, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        return vformat_arena(arena, fmt, ArrayRef<FormatArg>(arg_array, static_cast<isize>(sizeof...(Args))));
    }
}

void vprint(bool to_stderr, bool newline, StringRef fmt, ArrayRef<FormatArg> args) noexcept;

template <typename... Args> void print(StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        vprint(false, false, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vprint(false, false, fmt, ArrayRef<FormatArg>(arg_array, static_cast<isize>(sizeof...(Args))));
    }
}

template <typename... Args> void println(StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        vprint(false, true, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vprint(false, true, fmt, ArrayRef<FormatArg>(arg_array, static_cast<isize>(sizeof...(Args))));
    }
}

template <typename... Args> void eprint(StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        vprint(true, false, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vprint(true, false, fmt, ArrayRef<FormatArg>(arg_array, static_cast<isize>(sizeof...(Args))));
    }
}

template <typename... Args> void eprintln(StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        vprint(true, true, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vprint(true, true, fmt, ArrayRef<FormatArg>(arg_array, static_cast<isize>(sizeof...(Args))));
    }
}

} // namespace keel
