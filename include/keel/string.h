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

} // namespace keel
