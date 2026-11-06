#pragma once

#include "keel/core.h"
#include "keel/hash.h"

namespace keel {

namespace detail {
template <typename From, typename To>
concept ConvertibleTo = requires(From (*f)()) { static_cast<To>(f()); };

[[nodiscard]] constexpr isize cstr_len(const char* s) noexcept {
    if (!s) return 0;
    isize n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

[[nodiscard]] constexpr int char_cmp(const char* a, const char* b, isize n) noexcept {
    for (isize i = 0; i < n; ++i) {
        const u8 ca = static_cast<u8>(a[i]);
        const u8 cb = static_cast<u8>(b[i]);
        if (ca != cb) return (ca < cb) ? -1 : 1;
    }
    return 0;
}

[[nodiscard]] constexpr bool is_ascii_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

[[nodiscard]] constexpr char to_ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}
} // namespace detail

struct StringSplit;

class StringRef {
public:
    static constexpr isize kMaxSize = static_cast<isize>(~usize{0} >> 1);

    constexpr StringRef() noexcept = default;
    constexpr StringRef(decltype(nullptr)) noexcept : m_data(nullptr), m_size(0) {}
    constexpr StringRef(const char* cstr) noexcept : m_data(cstr), m_size(detail::cstr_len(cstr)) {}
    constexpr StringRef(const char* data, isize size) noexcept : m_data(data), m_size(size) {
        KEEL_ASSERT(size >= 0);
    }

    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, StringRef>) && (!IsSame<RemoveCvRef<R>, const char*>) &&
                (!IsSame<RemoveCvRef<R>, char*>) && requires(const R& r) {
                    { r.data() } -> detail::ConvertibleTo<const char*>;
                    { r.size() };
                }
    constexpr StringRef(const R& range) noexcept
        : m_data(range.data()), m_size(static_cast<isize>(range.size())) {}

    [[nodiscard]] constexpr const char* data() const noexcept { return m_data; }
    [[nodiscard]] constexpr isize size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] constexpr const char* begin() const noexcept { return m_data; }
    [[nodiscard]] constexpr const char* end() const noexcept { return m_data + m_size; }

    [[nodiscard]] constexpr char operator[](isize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        return m_data[i];
    }

    [[nodiscard]] constexpr char front() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] constexpr char back() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    constexpr void remove_prefix(isize n) noexcept {
        KEEL_BOUNDS_ASSERT(n >= 0 && n <= m_size);
        m_data += n;
        m_size -= n;
    }

    constexpr void remove_suffix(isize n) noexcept {
        KEEL_BOUNDS_ASSERT(n >= 0 && n <= m_size);
        m_size -= n;
    }

    [[nodiscard]] constexpr StringRef substr(isize offset, isize count = kMaxSize) const noexcept {
        KEEL_BOUNDS_ASSERT(offset >= 0 && offset <= m_size);
        const isize rem = m_size - offset;
        const isize len = (count < 0 || count > rem) ? rem : count;
        return StringRef(m_data + offset, len);
    }

    [[nodiscard]] constexpr bool starts_with(StringRef prefix) const noexcept {
        return m_size >= prefix.m_size && detail::char_cmp(m_data, prefix.m_data, prefix.m_size) == 0;
    }

    [[nodiscard]] constexpr bool starts_with(char c) const noexcept { return m_size > 0 && m_data[0] == c; }

    [[nodiscard]] constexpr bool ends_with(StringRef suffix) const noexcept {
        return m_size >= suffix.m_size &&
               detail::char_cmp(m_data + (m_size - suffix.m_size), suffix.m_data, suffix.m_size) == 0;
    }

    [[nodiscard]] constexpr bool ends_with(char c) const noexcept { return m_size > 0 && m_data[m_size - 1] == c; }

    [[nodiscard]] constexpr isize find(char c, isize from = 0) const noexcept {
        if (from < 0) from = 0;
        for (isize i = from; i < m_size; ++i) {
            if (m_data[i] == c) return i;
        }
        return kInvalidIndex;
    }

    [[nodiscard]] constexpr isize find(StringRef needle, isize from = 0) const noexcept {
        if (from < 0) from = 0;
        if (needle.m_size == 0) return (from <= m_size) ? from : kInvalidIndex;
        if (needle.m_size > m_size - from) return kInvalidIndex;
        const isize limit = m_size - needle.m_size;
        const char first = needle.m_data[0];
        for (isize i = from; i <= limit; ++i) {
            if (m_data[i] == first && detail::char_cmp(m_data + i, needle.m_data, needle.m_size) == 0) {
                return i;
            }
        }
        return kInvalidIndex;
    }

    [[nodiscard]] constexpr isize rfind(char c) const noexcept {
        for (isize i = m_size - 1; i >= 0; --i) {
            if (m_data[i] == c) return i;
        }
        return kInvalidIndex;
    }

    [[nodiscard]] constexpr isize rfind(StringRef needle) const noexcept {
        if (needle.m_size == 0) return m_size;
        if (needle.m_size > m_size) return kInvalidIndex;
        for (isize i = m_size - needle.m_size; i >= 0; --i) {
            if (detail::char_cmp(m_data + i, needle.m_data, needle.m_size) == 0) {
                return i;
            }
        }
        return kInvalidIndex;
    }

    [[nodiscard]] constexpr bool contains(char c) const noexcept { return find(c) != kInvalidIndex; }
    [[nodiscard]] constexpr bool contains(StringRef needle) const noexcept { return find(needle) != kInvalidIndex; }

    [[nodiscard]] constexpr StringRef trim_left() const noexcept {
        isize i = 0;
        while (i < m_size && detail::is_ascii_space(m_data[i])) ++i;
        return substr(i);
    }

    [[nodiscard]] constexpr StringRef trim_right() const noexcept {
        isize n = m_size;
        while (n > 0 && detail::is_ascii_space(m_data[n - 1])) --n;
        return substr(0, n);
    }

    [[nodiscard]] constexpr StringRef trim() const noexcept { return trim_left().trim_right(); }

    [[nodiscard]] constexpr StringSplit split_once(char delim) const noexcept;
    [[nodiscard]] constexpr StringSplit split_once(StringRef delim) const noexcept;

    [[nodiscard]] constexpr bool equals_ignore_ascii_case(StringRef other) const noexcept {
        if (m_size != other.m_size) return false;
        for (isize i = 0; i < m_size; ++i) {
            if (detail::to_ascii_lower(m_data[i]) != detail::to_ascii_lower(other.m_data[i])) return false;
        }
        return true;
    }

    [[nodiscard]] constexpr int compare(StringRef other) const noexcept {
        const isize common = keel::min(m_size, other.m_size);
        const int cmp = detail::char_cmp(m_data, other.m_data, common);
        if (cmp != 0) return cmp;
        return (m_size < other.m_size) ? -1 : ((m_size > other.m_size) ? 1 : 0);
    }

    [[nodiscard]] friend constexpr bool operator==(StringRef a, StringRef b) noexcept {
        return a.m_size == b.m_size && detail::char_cmp(a.m_data, b.m_data, a.m_size) == 0;
    }

    [[nodiscard]] friend constexpr bool operator!=(StringRef a, StringRef b) noexcept { return !(a == b); }
    [[nodiscard]] friend constexpr bool operator<(StringRef a, StringRef b) noexcept { return a.compare(b) < 0; }
    [[nodiscard]] friend constexpr bool operator<=(StringRef a, StringRef b) noexcept { return a.compare(b) <= 0; }
    [[nodiscard]] friend constexpr bool operator>(StringRef a, StringRef b) noexcept { return a.compare(b) > 0; }
    [[nodiscard]] friend constexpr bool operator>=(StringRef a, StringRef b) noexcept { return a.compare(b) >= 0; }

private:
    const char* m_data = nullptr;
    isize m_size = 0;
};

struct StringSplit {
    StringRef first{};
    StringRef second{};
    bool found = false;
};

constexpr StringSplit StringRef::split_once(char delim) const noexcept {
    const isize pos = find(delim);
    if (pos == kInvalidIndex) return {*this, StringRef{}, false};
    return {substr(0, pos), substr(pos + 1), true};
}

constexpr StringSplit StringRef::split_once(StringRef delim) const noexcept {
    const isize pos = find(delim);
    if (pos == kInvalidIndex) return {*this, StringRef{}, false};
    return {substr(0, pos), substr(pos + delim.size()), true};
}

template <> struct Hash<StringRef> {
    [[nodiscard]] u64 operator()(StringRef s) const noexcept {
        return hash_bytes(s.data(), static_cast<usize>(s.size()));
    }
};

template <typename T> class ArrayRef {
public:
    constexpr ArrayRef() noexcept = default;
    constexpr ArrayRef(decltype(nullptr)) noexcept : m_data(nullptr), m_size(0) {}
    constexpr ArrayRef(const T* data, isize size) noexcept : m_data(data), m_size(size) {
        KEEL_ASSERT(size >= 0);
    }

    template <usize N> constexpr ArrayRef(const T (&arr)[N]) noexcept : m_data(arr), m_size(static_cast<isize>(N)) {}

    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, ArrayRef<T>>) && requires(const R& r) {
            { r.data() } -> detail::ConvertibleTo<const T*>;
            { r.size() };
        }
    constexpr ArrayRef(const R& range) noexcept
        : m_data(range.data()), m_size(static_cast<isize>(range.size())) {}

    [[nodiscard]] constexpr const T* data() const noexcept { return m_data; }
    [[nodiscard]] constexpr isize size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] constexpr const T* begin() const noexcept { return m_data; }
    [[nodiscard]] constexpr const T* end() const noexcept { return m_data + m_size; }

    [[nodiscard]] constexpr const T& operator[](isize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        return m_data[i];
    }

    [[nodiscard]] constexpr const T& front() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] constexpr const T& back() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    [[nodiscard]] constexpr ArrayRef<T> slice(isize offset, isize count = StringRef::kMaxSize) const noexcept {
        KEEL_BOUNDS_ASSERT(offset >= 0 && offset <= m_size);
        const isize rem = m_size - offset;
        const isize len = (count < 0 || count > rem) ? rem : count;
        return ArrayRef<T>(m_data + offset, len);
    }

private:
    const T* m_data = nullptr;
    isize m_size = 0;
};

template <typename T> class MutArrayRef {
public:
    constexpr MutArrayRef() noexcept = default;
    constexpr MutArrayRef(decltype(nullptr)) noexcept : m_data(nullptr), m_size(0) {}
    constexpr MutArrayRef(T* data, isize size) noexcept : m_data(data), m_size(size) {
        KEEL_ASSERT(size >= 0);
    }

    template <usize N> constexpr MutArrayRef(T (&arr)[N]) noexcept : m_data(arr), m_size(static_cast<isize>(N)) {}

    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, MutArrayRef<T>>) && requires(R& r) {
            { r.data() } -> detail::ConvertibleTo<T*>;
            { r.size() };
        }
    constexpr MutArrayRef(R& range) noexcept
        : m_data(range.data()), m_size(static_cast<isize>(range.size())) {}

    [[nodiscard]] constexpr operator ArrayRef<T>() const noexcept { return ArrayRef<T>(m_data, m_size); }

    [[nodiscard]] constexpr T* data() const noexcept { return m_data; }
    [[nodiscard]] constexpr isize size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] constexpr T* begin() const noexcept { return m_data; }
    [[nodiscard]] constexpr T* end() const noexcept { return m_data + m_size; }

    [[nodiscard]] constexpr T& operator[](isize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        return m_data[i];
    }

    [[nodiscard]] constexpr T& front() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] constexpr T& back() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    [[nodiscard]] constexpr MutArrayRef<T> slice(isize offset, isize count = StringRef::kMaxSize) const noexcept {
        KEEL_BOUNDS_ASSERT(offset >= 0 && offset <= m_size);
        const isize rem = m_size - offset;
        const isize len = (count < 0 || count > rem) ? rem : count;
        return MutArrayRef<T>(m_data + offset, len);
    }

private:
    T* m_data = nullptr;
    isize m_size = 0;
};

} // namespace keel
