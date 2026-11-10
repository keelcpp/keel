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

    static String with_capacity(usize cap, Allocator* alloc = nullptr) {
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

    // Zero-header conversion to std::basic_string_view<char, ...>
    template <typename SV>
        requires(!IsSame<RemoveCvRef<SV>, String>) && (!IsSame<RemoveCvRef<SV>, StringRef>) &&
                IsTriviallyCopyable<SV> && requires(const char* d, usize n) {
                    typename SV::traits_type;
                    SV(d, n);
                }
    [[nodiscard]] operator SV() const noexcept {
        return SV(c_str(), m_size);
    }

    [[nodiscard]] const char* c_str() const noexcept { return m_data ? m_data : ""; }
    [[nodiscard]] char* data() noexcept { return m_data; }
    [[nodiscard]] const char* data() const noexcept { return c_str(); }

    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] usize length() const noexcept { return m_size; }
    [[nodiscard]] usize capacity() const noexcept { return m_cap; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

    [[nodiscard]] char* begin() noexcept { return data(); }
    [[nodiscard]] const char* begin() const noexcept { return data(); }
    [[nodiscard]] char* end() noexcept { return data() + m_size; }
    [[nodiscard]] const char* end() const noexcept { return data() + m_size; }

    [[nodiscard]] char& operator[](usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return m_data[i];
    }

    [[nodiscard]] char operator[](usize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return m_data[i];
    }

    void reserve(usize min_cap) {
        if (min_cap <= m_cap) return;
        usize new_cap = (m_cap == 0) ? 15 : m_cap;
        while (new_cap < min_cap) {
            new_cap = new_cap * 2 + 1;
        }
        reallocate(new_cap);
    }

    void clear() noexcept {
        m_size = 0;
        if (m_data) m_data[0] = '\0';
    }

    void resize(usize new_size, char fill = '\0') {
        if (new_size > m_cap) {
            reserve(new_size);
        }
        if (new_size > m_size) {
            mem_set(m_data + m_size, static_cast<u8>(fill), new_size - m_size);
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
        const usize n = str.size();
        if (n == 0) return;
        const char* src = str.data();
        // Handle self-aliasing if str points inside m_data
        if (m_data && src >= m_data && src <= m_data + m_size) {
            const usize offset = static_cast<usize>(src - m_data);
            reserve(m_size + n);
            mem_move(m_data + m_size, m_data + offset, n);
        } else {
            reserve(m_size + n);
            mem_copy(m_data + m_size, src, n);
        }
        m_size += n;
        m_data[m_size] = '\0';
    }

    void remove_at(usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        const usize tail = m_size - i - 1;
        if (tail > 0) {
            mem_move(m_data + i, m_data + i + 1, tail);
        }
        --m_size;
        m_data[m_size] = '\0';
    }

    void erase(usize i) noexcept { remove_at(i); }

    [[nodiscard]] StringRef substr(usize offset, usize count = StringRef::npos) const noexcept {
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
    void reallocate(usize new_cap) {
        const usize old_bytes = (m_data != nullptr) ? (m_cap + 1) : 0;
        m_data = static_cast<char*>(m_alloc->realloc_bytes(m_data, old_bytes, new_cap + 1, 1));
        m_cap = new_cap;
        m_data[m_size] = '\0';
    }

    char* m_data = nullptr;
    usize m_size = 0;
    usize m_cap = 0;
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

// Small-buffer optimized dynamic UTF-8 string: stores up to N - 1 chars (+ '\0') inline on the stack,
// and spills to Allocator when size >= N. Never stores a self-referential pointer (`m_heap == nullptr` when inline).
template <usize N = 64> class SmallString {
    static_assert(N > 1, "SmallString inline capacity N must be greater than 1");

public:
    explicit SmallString(Allocator* alloc = nullptr) noexcept
        : m_heap(nullptr), m_size(0), m_cap(N - 1), m_alloc(alloc ? alloc : default_allocator()) {
        m_inline[0] = '\0';
    }

    explicit SmallString(StringRef str, Allocator* alloc = nullptr)
        : m_heap(nullptr), m_size(0), m_cap(N - 1), m_alloc(alloc ? alloc : default_allocator()) {
        m_inline[0] = '\0';
        append(str);
    }

    explicit SmallString(const char* cstr, Allocator* alloc = nullptr)
        : SmallString(StringRef(cstr), alloc) {}

    static SmallString from(StringRef str, Allocator* alloc = nullptr) {
        return SmallString(str, alloc);
    }

    static SmallString with_capacity(usize cap, Allocator* alloc = nullptr) {
        SmallString s(alloc);
        s.reserve(cap);
        return s;
    }

    ~SmallString() noexcept {
        if (m_heap) {
            m_alloc->free_bytes(m_heap, m_cap + 1, 1);
        }
    }

    SmallString(const SmallString&) = delete;
    SmallString& operator=(const SmallString&) = delete;

    SmallString(SmallString&& other) noexcept
        : m_heap(nullptr), m_size(0), m_cap(N - 1), m_alloc(other.m_alloc) {
        move_from(other);
    }

    SmallString& operator=(SmallString&& other) noexcept {
        if (this != &other) {
            if (m_heap) {
                m_alloc->free_bytes(m_heap, m_cap + 1, 1);
                m_heap = nullptr;
                m_cap = N - 1;
            }
            m_alloc = other.m_alloc;
            move_from(other);
        }
        return *this;
    }

    [[nodiscard]] SmallString clone(Allocator* alloc = nullptr) const {
        return SmallString::from(as_ref(), alloc ? alloc : m_alloc);
    }

    [[nodiscard]] operator StringRef() const noexcept { return StringRef(c_str(), m_size); }
    [[nodiscard]] StringRef as_ref() const noexcept { return StringRef(c_str(), m_size); }

    // Zero-header conversion to std::basic_string_view<char, ...>
    template <typename SV>
        requires(!IsSame<RemoveCvRef<SV>, SmallString>) && (!IsSame<RemoveCvRef<SV>, StringRef>) &&
                IsTriviallyCopyable<SV> && requires(const char* d, usize n) {
                    typename SV::traits_type;
                    SV(d, n);
                }
    [[nodiscard]] operator SV() const noexcept {
        return SV(c_str(), m_size);
    }

    [[nodiscard]] const char* c_str() const noexcept { return m_heap ? m_heap : m_inline; }
    [[nodiscard]] char* data() noexcept { return m_heap ? m_heap : m_inline; }
    [[nodiscard]] const char* data() const noexcept { return m_heap ? m_heap : m_inline; }

    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] usize length() const noexcept { return m_size; }
    [[nodiscard]] usize capacity() const noexcept { return m_cap; }
    [[nodiscard]] static constexpr usize inline_capacity() noexcept { return N - 1; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool is_inline() const noexcept { return m_heap == nullptr; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

    [[nodiscard]] char* begin() noexcept { return data(); }
    [[nodiscard]] const char* begin() const noexcept { return data(); }
    [[nodiscard]] char* end() noexcept { return data() + m_size; }
    [[nodiscard]] const char* end() const noexcept { return data() + m_size; }

    [[nodiscard]] char& operator[](usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return data()[i];
    }

    [[nodiscard]] char operator[](usize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return data()[i];
    }

    void reserve(usize min_cap) {
        if (min_cap <= m_cap) return;
        usize new_cap = m_cap;
        while (new_cap < min_cap) {
            new_cap = new_cap * 2 + 1;
        }
        reallocate(new_cap);
    }

    void clear() noexcept {
        m_size = 0;
        data()[0] = '\0';
    }

    void resize(usize new_size, char fill = '\0') {
        if (new_size > m_cap) {
            reserve(new_size);
        }
        char* cur = data();
        if (new_size > m_size) {
            mem_set(cur + m_size, static_cast<u8>(fill), new_size - m_size);
        }
        m_size = new_size;
        cur[m_size] = '\0';
    }

    void assign(StringRef str) {
        const char* cur = data();
        if (str.data() >= cur && str.data() <= cur + m_size) {
            const usize offset = static_cast<usize>(str.data() - cur);
            const usize n = str.size();
            if (n > 0 && offset > 0) {
                mem_move(data(), data() + offset, n);
            }
            m_size = n;
            data()[m_size] = '\0';
            return;
        }
        clear();
        append(str);
    }

    void push_back(char c) {
        if (m_size == m_cap) [[unlikely]] {
            reserve(m_cap * 2 + 1);
        }
        char* cur = data();
        cur[m_size++] = c;
        cur[m_size] = '\0';
    }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        data()[m_size] = '\0';
    }

    void append(StringRef str) {
        const usize n = str.size();
        if (n == 0) return;
        const char* cur = data();
        const char* src = str.data();
        if (src >= cur && src <= cur + m_size) {
            const usize offset = static_cast<usize>(src - cur);
            reserve(m_size + n);
            char* dst = data();
            mem_move(dst + m_size, dst + offset, n);
        } else {
            reserve(m_size + n);
            char* dst = data();
            mem_copy(dst + m_size, src, n);
        }
        m_size += n;
        data()[m_size] = '\0';
    }

    void remove_at(usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        char* cur = data();
        const usize tail = m_size - i - 1;
        if (tail > 0) {
            mem_move(cur + i, cur + i + 1, tail);
        }
        --m_size;
        cur[m_size] = '\0';
    }

    void erase(usize i) noexcept { remove_at(i); }

    [[nodiscard]] StringRef substr(usize offset, usize count = StringRef::npos) const noexcept {
        return as_ref().substr(offset, count);
    }

    [[nodiscard]] friend bool operator==(const SmallString& a, const SmallString& b) noexcept {
        return a.as_ref() == b.as_ref();
    }
    [[nodiscard]] friend bool operator==(const SmallString& a, StringRef b) noexcept { return a.as_ref() == b; }
    [[nodiscard]] friend bool operator==(StringRef a, const SmallString& b) noexcept { return a == b.as_ref(); }
    [[nodiscard]] friend bool operator==(const SmallString& a, const char* b) noexcept {
        return a.as_ref() == StringRef(b);
    }
    [[nodiscard]] friend bool operator!=(const SmallString& a, const SmallString& b) noexcept { return !(a == b); }
    [[nodiscard]] friend bool operator!=(const SmallString& a, StringRef b) noexcept { return !(a == b); }
    [[nodiscard]] friend bool operator!=(const SmallString& a, const char* b) noexcept { return !(a == b); }

private:
    void move_from(SmallString& other) noexcept {
        if (other.is_inline()) {
            m_heap = nullptr;
            m_cap = N - 1;
            m_size = other.m_size;
            if (m_size > 0) {
                mem_copy(m_inline, other.m_inline, m_size);
            }
            m_inline[m_size] = '\0';
            other.m_size = 0;
            other.m_inline[0] = '\0';
        } else {
            m_heap = other.m_heap;
            m_size = other.m_size;
            m_cap = other.m_cap;
            other.m_heap = nullptr;
            other.m_size = 0;
            other.m_cap = N - 1;
            other.m_inline[0] = '\0';
        }
    }

    void reallocate(usize new_cap) {
        if (m_heap) {
            m_heap = static_cast<char*>(m_alloc->realloc_bytes(m_heap, m_cap + 1, new_cap + 1, 1));
        } else {
            char* new_buf = static_cast<char*>(m_alloc->alloc_bytes(new_cap + 1, 1));
            if (m_size > 0) {
                mem_copy(new_buf, m_inline, m_size);
            }
            m_heap = new_buf;
        }
        m_cap = new_cap;
        m_heap[m_size] = '\0';
    }

    char* m_heap = nullptr;
    usize m_size = 0;
    usize m_cap = N - 1;
    Allocator* m_alloc = nullptr;
    char m_inline[N]{};
};

template <usize N> struct Hash<SmallString<N>> {
    [[nodiscard]] u64 operator()(const SmallString<N>& s) const noexcept { return Hash<StringRef>{}(s.as_ref()); }
    [[nodiscard]] u64 operator()(StringRef s) const noexcept { return Hash<StringRef>{}(s); }
    [[nodiscard]] u64 operator()(const char* s) const noexcept { return Hash<StringRef>{}(StringRef(s)); }
};

template <usize N> struct Equal<SmallString<N>> {
    [[nodiscard]] bool operator()(const SmallString<N>& a, const SmallString<N>& b) const noexcept {
        return a.as_ref() == b.as_ref();
    }
    [[nodiscard]] bool operator()(const SmallString<N>& a, StringRef b) const noexcept {
        return a.as_ref() == b;
    }
    [[nodiscard]] bool operator()(const SmallString<N>& a, const char* b) const noexcept {
        return a == b;
    }
};

// Pure stack-allocated fixed-capacity UTF-8 string (Rule of Zero, trivially copyable, zero heap allocations).
// Stores up to N - 1 characters (+ '\0') directly inside `m_buf[N]`; excess characters are safely truncated.
template <usize N = 64> class FixedString {
    static_assert(N > 1, "FixedString capacity N must be greater than 1");

public:
    constexpr FixedString() noexcept = default;

    constexpr FixedString(const char* cstr) noexcept { set(StringRef(cstr)); }

    constexpr FixedString(const char* str, usize len) noexcept { set(StringRef(str, len)); }

    constexpr FixedString(StringRef ref) noexcept { set(ref); }

    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, FixedString>) && (!IsSame<RemoveCvRef<R>, StringRef>) &&
                (!IsSame<RemoveCvRef<R>, const char*>) && (!IsSame<RemoveCvRef<R>, char*>) &&
                requires(const R& r) {
                    { r.data() } -> detail::ConvertibleTo<const char*>;
                    { r.size() };
                }
    constexpr FixedString(const R& range) noexcept {
        set(StringRef(range.data(), static_cast<usize>(range.size())));
    }

    constexpr FixedString& operator=(const char* cstr) noexcept {
        set(StringRef(cstr));
        return *this;
    }

    constexpr FixedString& operator=(StringRef ref) noexcept {
        set(ref);
        return *this;
    }

    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, FixedString>) && (!IsSame<RemoveCvRef<R>, StringRef>) &&
                (!IsSame<RemoveCvRef<R>, const char*>) && (!IsSame<RemoveCvRef<R>, char*>) &&
                requires(const R& r) {
                    { r.data() } -> detail::ConvertibleTo<const char*>;
                    { r.size() };
                }
    constexpr FixedString& operator=(const R& range) noexcept {
        set(StringRef(range.data(), static_cast<usize>(range.size())));
        return *this;
    }

    constexpr void set(StringRef ref) noexcept {
        const usize clamped = keel::min<usize>(ref.size(), N - 1);
        m_size = static_cast<u32>(clamped);
        if (__builtin_is_constant_evaluated()) {
            for (u32 i = 0; i < m_size; ++i) {
                m_buf[i] = ref.data()[i];
            }
        } else if (m_size > 0) {
            mem_move(m_buf, ref.data(), static_cast<usize>(m_size));
        }
        m_buf[m_size] = '\0';
    }

    constexpr void clear() noexcept {
        m_buf[0] = '\0';
        m_size = 0;
    }

    constexpr void push_back(char c) noexcept {
        if (static_cast<usize>(m_size) < N - 1) {
            m_buf[m_size++] = c;
            m_buf[m_size] = '\0';
        }
    }

    constexpr void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        m_buf[m_size] = '\0';
    }

    constexpr void append(StringRef ref) noexcept {
        const usize avail = (N - 1) - static_cast<usize>(m_size);
        const usize to_copy = keel::min<usize>(ref.size(), avail);
        if (to_copy == 0) return;
        if (__builtin_is_constant_evaluated()) {
            for (usize i = 0; i < to_copy; ++i) {
                m_buf[m_size + i] = ref.data()[i];
            }
        } else {
            mem_move(m_buf + m_size, ref.data(), to_copy);
        }
        m_size += static_cast<u32>(to_copy);
        m_buf[m_size] = '\0';
    }

    constexpr void resize(usize new_size, char fill = '\0') noexcept {
        const u32 target = static_cast<u32>(keel::min<usize>(new_size, N - 1));
        if (target > m_size) {
            for (u32 i = m_size; i < target; ++i) {
                m_buf[i] = fill;
            }
        }
        m_size = target;
        m_buf[m_size] = '\0';
    }

    [[nodiscard]] constexpr char* inline_buffer() noexcept { return m_buf; }

    constexpr void set_inline_size(usize len) noexcept {
        const usize clamped = keel::min<usize>(len, N - 1);
        m_size = static_cast<u32>(clamped);
        m_buf[m_size] = '\0';
    }

    [[nodiscard]] constexpr operator StringRef() const noexcept {
        return StringRef(m_buf, static_cast<usize>(m_size));
    }

    [[nodiscard]] constexpr StringRef as_ref() const noexcept {
        return StringRef(m_buf, static_cast<usize>(m_size));
    }

    // Zero-header conversion to std::basic_string_view<char, ...>
    template <typename SV>
        requires(!IsSame<RemoveCvRef<SV>, FixedString>) && (!IsSame<RemoveCvRef<SV>, StringRef>) &&
                IsTriviallyCopyable<SV> && requires(const char* d, usize n) {
                    typename SV::traits_type;
                    SV(d, n);
                }
    [[nodiscard]] constexpr operator SV() const noexcept {
        return SV(m_buf, static_cast<usize>(m_size));
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept { return m_buf; }
    [[nodiscard]] constexpr char* data() noexcept { return m_buf; }
    [[nodiscard]] constexpr const char* data() const noexcept { return m_buf; }
    [[nodiscard]] constexpr usize size() const noexcept { return static_cast<usize>(m_size); }
    [[nodiscard]] constexpr usize length() const noexcept { return static_cast<usize>(m_size); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] constexpr bool full() const noexcept { return static_cast<usize>(m_size) == N - 1; }
    [[nodiscard]] static constexpr usize capacity() noexcept { return N - 1; }

    [[nodiscard]] constexpr char* begin() noexcept { return m_buf; }
    [[nodiscard]] constexpr const char* begin() const noexcept { return m_buf; }
    [[nodiscard]] constexpr char* end() noexcept { return m_buf + m_size; }
    [[nodiscard]] constexpr const char* end() const noexcept { return m_buf + m_size; }

    [[nodiscard]] constexpr char& operator[](usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < static_cast<usize>(m_size));
        return m_buf[i];
    }

    [[nodiscard]] constexpr char operator[](usize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i < static_cast<usize>(m_size));
        return m_buf[i];
    }

    [[nodiscard]] constexpr StringRef substr(usize offset, usize count = StringRef::npos) const noexcept {
        return as_ref().substr(offset, count);
    }

    [[nodiscard]] constexpr bool operator==(const FixedString& other) const noexcept {
        return as_ref() == other.as_ref();
    }
    [[nodiscard]] constexpr bool operator==(StringRef other) const noexcept { return as_ref() == other; }
    [[nodiscard]] constexpr bool operator==(const char* other) const noexcept {
        return other ? (as_ref() == StringRef(other)) : empty();
    }

    template <typename R>
        requires(!IsSame<RemoveCvRef<R>, FixedString>) && (!IsSame<RemoveCvRef<R>, StringRef>) &&
                (!IsSame<RemoveCvRef<R>, const char*>) && (!IsSame<RemoveCvRef<R>, char*>) &&
                requires(const R& r) {
                    { r.data() } -> detail::ConvertibleTo<const char*>;
                    { r.size() };
                }
    [[nodiscard]] constexpr bool operator==(const R& other) const noexcept {
        return as_ref() == StringRef(other.data(), static_cast<usize>(other.size()));
    }

private:
    u32 m_size = 0;
    char m_buf[N]{};
};

static_assert(IsTriviallyCopyable<FixedString<64>>, "FixedString must remain trivially copyable (Rule of Zero)");

template <usize N> struct Hash<FixedString<N>> {
    [[nodiscard]] u64 operator()(const FixedString<N>& s) const noexcept { return Hash<StringRef>{}(s.as_ref()); }
    [[nodiscard]] u64 operator()(StringRef s) const noexcept { return Hash<StringRef>{}(s); }
    [[nodiscard]] u64 operator()(const char* s) const noexcept { return Hash<StringRef>{}(StringRef(s)); }
};

template <usize N> struct Equal<FixedString<N>> {
    [[nodiscard]] bool operator()(const FixedString<N>& a, const FixedString<N>& b) const noexcept {
        return a.as_ref() == b.as_ref();
    }
    [[nodiscard]] bool operator()(const FixedString<N>& a, StringRef b) const noexcept {
        return a.as_ref() == b;
    }
    [[nodiscard]] bool operator()(const FixedString<N>& a, const char* b) const noexcept {
        return a == b;
    }
};

} // namespace keel
