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
    usize str_len = 0;

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
    template <usize N>
    FormatArg(const SmallString<N>& s) noexcept
        : kind(Kind::StringRef), cstr_val(s.data()), str_len(s.size()) {}
    template <usize N>
    constexpr FormatArg(const FixedString<N>& s) noexcept
        : kind(Kind::StringRef), cstr_val(s.data()), str_len(s.size()) {}

    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, FormatArg>) && (!IsSame<RemoveCvRef<R>, StringRef>) &&
                (!IsSame<RemoveCvRef<R>, String>) && (!IsSame<RemoveCvRef<R>, const char*>) &&
                (!IsSame<RemoveCvRef<R>, char*>) && requires(const R& r) {
                    { r.data() } -> detail::ConvertibleTo<const char*>;
                    { r.size() };
                }
    constexpr FormatArg(const R& range) noexcept
        : kind(Kind::StringRef), cstr_val(range.data()), str_len(static_cast<usize>(range.size())) {}

    template <typename T>
        requires(!IsSame<RemoveCv<T>, char>)
    constexpr FormatArg(const T* p) noexcept : kind(Kind::Pointer), ptr_val(static_cast<const void*>(p)) {}
    constexpr FormatArg(decltype(nullptr)) noexcept : kind(Kind::Pointer), ptr_val(nullptr) {}
};

// Low-level type-erased printf/brace formatter (`%d`, `%u`, `%x`, `%X`, `%f`, `%.2f`, `%g`, `%s`, `%S`, `%c`, `%p`, `{}`)
void vformat_into(String& out, StringRef fmt, ArrayRef<FormatArg> args);
usize vformat_buf(char* buf, usize cap, StringRef fmt, ArrayRef<FormatArg> args) noexcept;
StringRef vformat_arena(Arena& arena, StringRef fmt, ArrayRef<FormatArg> args);

template <typename... Args> void format_into(String& out, StringRef fmt, const Args&... args) {
    if constexpr (sizeof...(Args) == 0) {
        vformat_into(out, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vformat_into(out, fmt, ArrayRef<FormatArg>(arg_array, sizeof...(Args)));
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

template <typename... Args> usize format_buf(char* buf, usize cap, StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        return vformat_buf(buf, cap, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        return vformat_buf(buf, cap, fmt, ArrayRef<FormatArg>(arg_array, sizeof...(Args)));
    }
}

template <usize N, typename... Args>
[[nodiscard]] inline StringRef format_buf(char (&buf)[N], StringRef fmt, const Args&... args) noexcept {
    const usize len = format_buf(buf, N, fmt, args...);
    return StringRef(buf, len);
}

template <usize N, typename... Args>
void format_into(FixedString<N>& out, StringRef fmt, const Args&... args) noexcept {
    const usize len = format_buf(out.inline_buffer(), N, fmt, args...);
    out.set_inline_size(len);
}

template <usize N = 64, typename... Args>
[[nodiscard]] FixedString<N> format_fixed(StringRef fmt, const Args&... args) noexcept {
    FixedString<N> out;
    format_into(out, fmt, args...);
    return out;
}

template <typename... Args> [[nodiscard]] StringRef format_arena(Arena& arena, StringRef fmt, const Args&... args) {
    if constexpr (sizeof...(Args) == 0) {
        return vformat_arena(arena, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        return vformat_arena(arena, fmt, ArrayRef<FormatArg>(arg_array, sizeof...(Args)));
    }
}

template <usize N, typename... Args>
void format_into(SmallString<N>& out, StringRef fmt, const Args&... args) {
    ArenaTemp scratch = get_scratch(out.allocator());
    const StringRef sv = format_arena(scratch.arena(), fmt, args...);
    out.append(sv);
}

template <usize N = 64, typename... Args>
[[nodiscard]] SmallString<N> format_small(StringRef fmt, const Args&... args) {
    SmallString<N> out;
    format_into(out, fmt, args...);
    return out;
}

void vprint(bool to_stderr, bool newline, StringRef fmt, ArrayRef<FormatArg> args) noexcept;

template <typename... Args> void print(StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        vprint(false, false, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vprint(false, false, fmt, ArrayRef<FormatArg>(arg_array, sizeof...(Args)));
    }
}

template <typename... Args> void println(StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        vprint(false, true, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vprint(false, true, fmt, ArrayRef<FormatArg>(arg_array, sizeof...(Args)));
    }
}

template <typename... Args> void eprint(StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        vprint(true, false, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vprint(true, false, fmt, ArrayRef<FormatArg>(arg_array, sizeof...(Args)));
    }
}

template <typename... Args> void eprintln(StringRef fmt, const Args&... args) noexcept {
    if constexpr (sizeof...(Args) == 0) {
        vprint(true, true, fmt, ArrayRef<FormatArg>{});
    } else {
        const FormatArg arg_array[] = {FormatArg(args)...};
        vprint(true, true, fmt, ArrayRef<FormatArg>(arg_array, sizeof...(Args)));
    }
}

} // namespace keel
