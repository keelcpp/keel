#pragma once

#include "keel/memory.h"
#include "keel/ref.h"

namespace keel {

// Move-only UTF-8 string without SSO. Always null-terminated via c_str().
class String {
public:
    explicit String(Allocator* alloc = nullptr) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    static String from(StringRef str, Allocator* alloc = nullptr) {
        String s(alloc);
        s.append(str);
        return s;
    }

    static String with_capacity(isize cap, Allocator* alloc = nullptr) {
        String s(alloc);
        s.reserve(cap);
        return s;
    }

    ~String() noexcept {
        if (m_data) {
            m_alloc->free_bytes(m_data, m_cap + 1, 1);
        }
    }

    String(const String&) = delete;
    String& operator=(const String&) = delete;

    String(String&& other) noexcept
        : m_data(other.m_data), m_size(other.m_size), m_cap(other.m_cap), m_alloc(other.m_alloc) {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_cap = 0;
    }

    String& operator=(String&& other) noexcept {
        if (this != &other) {
            if (m_data) m_alloc->free_bytes(m_data, m_cap + 1, 1);
            m_data = other.m_data;
            m_size = other.m_size;
            m_cap = other.m_cap;
            m_alloc = other.m_alloc;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_cap = 0;
        }
        return *this;
    }

    [[nodiscard]] String clone(Allocator* alloc = nullptr) const {
        return String::from(as_ref(), alloc ? alloc : m_alloc);
    }

    [[nodiscard]] operator StringRef() const noexcept { return StringRef(c_str(), m_size); }
    [[nodiscard]] StringRef as_ref() const noexcept { return StringRef(c_str(), m_size); }

    [[nodiscard]] const char* c_str() const noexcept { return m_data ? m_data : ""; }
    [[nodiscard]] char* data() noexcept { return m_data ? m_data : const_cast<char*>(""); }
    [[nodiscard]] const char* data() const noexcept { return c_str(); }

    [[nodiscard]] isize size() const noexcept { return m_size; }
    [[nodiscard]] isize capacity() const noexcept { return m_cap; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

    [[nodiscard]] char* begin() noexcept { return data(); }
    [[nodiscard]] const char* begin() const noexcept { return data(); }
    [[nodiscard]] char* end() noexcept { return data() + m_size; }
    [[nodiscard]] const char* end() const noexcept { return data() + m_size; }

    [[nodiscard]] char& operator[](isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        return m_data[i];
    }

    [[nodiscard]] char operator[](isize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        return m_data[i];
    }

    void reserve(isize min_cap) {
        if (min_cap <= m_cap) return;
        isize new_cap = (m_cap == 0) ? 15 : m_cap;
        while (new_cap < min_cap) {
            new_cap = new_cap * 2 + 1;
        }
        reallocate(new_cap);
    }

    void clear() noexcept {
        m_size = 0;
        if (m_data) m_data[0] = '\0';
    }

    void resize(isize new_size, char fill = '\0') {
        KEEL_ASSERT(new_size >= 0);
        if (new_size > m_cap) {
            reserve(new_size);
        }
        if (new_size > m_size) {
            mem_set(m_data + m_size, static_cast<u8>(fill), static_cast<usize>(new_size - m_size));
        }
        m_size = new_size;
        if (m_data) m_data[m_size] = '\0';
    }

    void push_back(char c) {
        if (m_size == m_cap) [[unlikely]] {
            reserve(m_cap == 0 ? 15 : (m_cap * 2 + 1));
        }
        m_data[m_size++] = c;
        m_data[m_size] = '\0';
    }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        m_data[m_size] = '\0';
    }

    void append(StringRef str) {
        const isize n = str.size();
        if (n == 0) return;
        const char* src = str.data();
        // Handle self-aliasing if str points inside m_data
        if (m_data && src >= m_data && src <= m_data + m_size) {
            const isize offset = static_cast<isize>(src - m_data);
            reserve(m_size + n);
            mem_move(m_data + m_size, m_data + offset, static_cast<usize>(n));
        } else {
            reserve(m_size + n);
            mem_copy(m_data + m_size, src, static_cast<usize>(n));
        }
        m_size += n;
        m_data[m_size] = '\0';
    }

    void remove_at(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        const isize tail = m_size - i - 1;
        if (tail > 0) {
            mem_move(m_data + i, m_data + i + 1, static_cast<usize>(tail));
        }
        --m_size;
        m_data[m_size] = '\0';
    }

    [[nodiscard]] StringRef substr(isize offset, isize count = StringRef::kMaxSize) const noexcept {
        return as_ref().substr(offset, count);
    }

    [[nodiscard]] friend bool operator==(const String& a, const String& b) noexcept {
        return a.as_ref() == b.as_ref();
    }
    [[nodiscard]] friend bool operator==(const String& a, StringRef b) noexcept { return a.as_ref() == b; }
    [[nodiscard]] friend bool operator==(StringRef a, const String& b) noexcept { return a == b.as_ref(); }
    [[nodiscard]] friend bool operator==(const String& a, const char* b) noexcept {
        return a.as_ref() == StringRef(b);
    }
    [[nodiscard]] friend bool operator!=(const String& a, const String& b) noexcept { return !(a == b); }
    [[nodiscard]] friend bool operator!=(const String& a, StringRef b) noexcept { return !(a == b); }
    [[nodiscard]] friend bool operator!=(const String& a, const char* b) noexcept { return !(a == b); }

private:
    void reallocate(isize new_cap) {
        const isize old_bytes = (m_data != nullptr) ? (m_cap + 1) : 0;
        m_data = static_cast<char*>(m_alloc->realloc_bytes(m_data, old_bytes, new_cap + 1, 1));
        m_cap = new_cap;
        m_data[m_size] = '\0';
    }

    char* m_data = nullptr;
    isize m_size = 0;
    isize m_cap = 0;
    Allocator* m_alloc = nullptr;
};

template <> struct Hash<String> {
    [[nodiscard]] u64 operator()(const String& s) const noexcept { return Hash<StringRef>{}(s.as_ref()); }
    [[nodiscard]] u64 operator()(StringRef s) const noexcept { return Hash<StringRef>{}(s); }
    [[nodiscard]] u64 operator()(const char* s) const noexcept { return Hash<StringRef>{}(StringRef(s)); }
};

template <> struct Equal<String> {
    [[nodiscard]] bool operator()(const String& a, const String& b) const noexcept { return a.as_ref() == b.as_ref(); }
    [[nodiscard]] bool operator()(const String& a, StringRef b) const noexcept { return a.as_ref() == b; }
    [[nodiscard]] bool operator()(const String& a, const char* b) const noexcept {
        return a.as_ref() == StringRef(b);
    }
};

// Zero-heap hybrid fixed-capacity string (Rule of Zero, trivially copyable).
// Holds either a pointer to a static/arena null-terminated string literal (zero copies, m_ptr != nullptr),
// or an inline fixed buffer (zero heap allocations, m_ptr == nullptr).
template <isize N = 64> class InplaceString {
    static_assert(N > 1, "InplaceString capacity N must be greater than 1");

public:
    constexpr InplaceString() noexcept : m_ptr(nullptr), m_size(0) { m_buf[0] = '\0'; }

    // Constructs from a static string literal or null-terminated string with zero copy.
    constexpr InplaceString(const char* literal) noexcept { set_static(literal); }

    // Constructs from a null-terminated external/arena string with known length (zero copy).
    constexpr InplaceString(const char* external_null_terminated, isize len) noexcept {
        set_static(external_null_terminated, len);
    }

    // Constructs from a StringRef, copying characters into the inline buffer.
    constexpr InplaceString(StringRef ref) noexcept { set(ref); }

    // Constructs from any string-like range (e.g. std::string_view, std::string, keel::String), copying into inline buffer.
    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, InplaceString>) && (!IsSame<RemoveCvRef<R>, StringRef>) &&
                (!IsSame<RemoveCvRef<R>, const char*>) && (!IsSame<RemoveCvRef<R>, char*>) &&
                requires(const R& r) {
                    { r.data() } -> detail::ConvertibleTo<const char*>;
                    { r.size() };
                }
    constexpr InplaceString(const R& range) noexcept {
        set(StringRef(range.data(), static_cast<isize>(range.size())));
    }

    constexpr InplaceString& operator=(const char* literal) noexcept {
        set_static(literal);
        return *this;
    }

    constexpr InplaceString& operator=(StringRef ref) noexcept {
        set(ref);
        return *this;
    }

    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, InplaceString>) && (!IsSame<RemoveCvRef<R>, StringRef>) &&
                (!IsSame<RemoveCvRef<R>, const char*>) && (!IsSame<RemoveCvRef<R>, char*>) &&
                requires(const R& r) {
                    { r.data() } -> detail::ConvertibleTo<const char*>;
                    { r.size() };
                }
    constexpr InplaceString& operator=(const R& range) noexcept {
        set(StringRef(range.data(), static_cast<isize>(range.size())));
        return *this;
    }

    constexpr void set_static(const char* literal) noexcept {
        if (literal) {
            m_ptr = literal;
            m_size = static_cast<u32>(detail::cstr_len(literal));
            m_buf[0] = '\0';
        } else {
            clear();
        }
    }

    constexpr void set_static(const char* external_null_terminated, isize len) noexcept {
        if (external_null_terminated && len >= 0) {
            m_ptr = external_null_terminated;
            m_size = static_cast<u32>(len);
            m_buf[0] = '\0';
        } else {
            clear();
        }
    }

    constexpr void set(StringRef ref) noexcept {
        const isize clamped = keel::min<isize>(ref.size() < 0 ? 0 : ref.size(), N - 1);
        m_size = static_cast<u32>(clamped);
        if (__builtin_is_constant_evaluated()) {
            for (u32 i = 0; i < m_size; ++i) {
                m_buf[i] = ref.data()[i];
            }
        } else if (m_size > 0) {
            mem_copy(m_buf, ref.data(), static_cast<usize>(m_size));
        }
        m_buf[m_size] = '\0';
        m_ptr = nullptr;
    }

    constexpr void clear() noexcept {
        m_buf[0] = '\0';
        m_ptr = nullptr;
        m_size = 0;
    }

    [[nodiscard]] constexpr char* inline_buffer() noexcept {
        m_ptr = nullptr;
        return m_buf;
    }

    constexpr void set_inline_size(isize len) noexcept {
        const isize clamped = keel::min<isize>(len < 0 ? 0 : len, N - 1);
        m_size = static_cast<u32>(clamped);
        m_buf[m_size] = '\0';
        m_ptr = nullptr;
    }

    [[nodiscard]] constexpr StringRef as_ref() const noexcept {
        return StringRef(data(), static_cast<isize>(m_size));
    }

    // Zero-header conversion to std::basic_string_view<char, ...>
    template <typename SV>
        requires IsTriviallyCopyable<SV> && requires(const char* d, usize n) {
            typename SV::traits_type;
            SV(d, n);
        }
    [[nodiscard]] constexpr operator SV() const noexcept {
        return SV(data(), static_cast<usize>(m_size));
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept { return m_ptr ? m_ptr : m_buf; }
    [[nodiscard]] constexpr const char* data() const noexcept { return m_ptr ? m_ptr : m_buf; }
    [[nodiscard]] constexpr isize size() const noexcept { return static_cast<isize>(m_size); }
    [[nodiscard]] constexpr isize length() const noexcept { return static_cast<isize>(m_size); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] constexpr bool is_static() const noexcept { return m_ptr != nullptr; }
    [[nodiscard]] static constexpr isize capacity() noexcept { return N - 1; }

    [[nodiscard]] constexpr const char* begin() const noexcept { return data(); }
    [[nodiscard]] constexpr const char* end() const noexcept { return data() + m_size; }

    [[nodiscard]] constexpr char operator[](isize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < static_cast<isize>(m_size));
        return data()[i];
    }

    [[nodiscard]] constexpr bool operator==(const InplaceString& other) const noexcept {
        return as_ref() == other.as_ref();
    }
    [[nodiscard]] constexpr bool operator==(StringRef other) const noexcept { return as_ref() == other; }
    [[nodiscard]] constexpr bool operator==(const char* other) const noexcept {
        return other ? (as_ref() == StringRef(other)) : empty();
    }

    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, InplaceString>) && (!IsSame<RemoveCvRef<R>, StringRef>) &&
                (!IsSame<RemoveCvRef<R>, const char*>) && (!IsSame<RemoveCvRef<R>, char*>) &&
                requires(const R& r) {
                    { r.data() } -> detail::ConvertibleTo<const char*>;
                    { r.size() };
                }
    [[nodiscard]] constexpr bool operator==(const R& other) const noexcept {
        return as_ref() == StringRef(other.data(), static_cast<isize>(other.size()));
    }

private:
    const char* m_ptr = nullptr;
    u32 m_size = 0;
    char m_buf[static_cast<usize>(N)]{};
};

static_assert(IsTriviallyCopyable<InplaceString<64>>, "InplaceString must remain trivially copyable (Rule of Zero)");

template <isize N> struct Hash<InplaceString<N>> {
    [[nodiscard]] u64 operator()(const InplaceString<N>& s) const noexcept { return Hash<StringRef>{}(s.as_ref()); }
    [[nodiscard]] u64 operator()(StringRef s) const noexcept { return Hash<StringRef>{}(s); }
    [[nodiscard]] u64 operator()(const char* s) const noexcept { return Hash<StringRef>{}(StringRef(s)); }
};

template <isize N> struct Equal<InplaceString<N>> {
    [[nodiscard]] bool operator()(const InplaceString<N>& a, const InplaceString<N>& b) const noexcept {
        return a.as_ref() == b.as_ref();
    }
    [[nodiscard]] bool operator()(const InplaceString<N>& a, StringRef b) const noexcept {
        return a.as_ref() == b;
    }
    [[nodiscard]] bool operator()(const InplaceString<N>& a, const char* b) const noexcept {
        return a == b;
    }
};

} // namespace keel
