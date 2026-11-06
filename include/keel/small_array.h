#pragma once

#include "keel/memory.h"
#include "keel/ref.h"

namespace keel {

// Fixed-capacity inline array with zero heap allocations.
template <typename T, isize N> class InplaceArray {
    static_assert(N > 0, "InplaceArray capacity N must be positive");
    static_assert(IsNothrowMoveConstructible<T>, "InplaceArray<T, N> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "InplaceArray<T, N> requires noexcept destructor");

public:
    InplaceArray() noexcept = default;
    ~InplaceArray() noexcept { clear(); }

    InplaceArray(const InplaceArray&) = delete;
    InplaceArray& operator=(const InplaceArray&) = delete;

    InplaceArray(InplaceArray&& other) noexcept : m_size(other.m_size) {
        for (isize i = 0; i < m_size; ++i) {
            ::new (ptr_at(i)) T(move(other[i]));
            if constexpr (!IsTriviallyDestructible<T>) other[i].~T();
        }
        other.m_size = 0;
    }

    InplaceArray& operator=(InplaceArray&& other) noexcept {
        if (this != &other) {
            clear();
            m_size = other.m_size;
            for (isize i = 0; i < m_size; ++i) {
                ::new (ptr_at(i)) T(move(other[i]));
                if constexpr (!IsTriviallyDestructible<T>) other[i].~T();
            }
            other.m_size = 0;
        }
        return *this;
    }

    [[nodiscard]] InplaceArray clone() const {
        InplaceArray copy;
        for (isize i = 0; i < m_size; ++i) {
            copy.push_back((*this)[i]);
        }
        return copy;
    }

    [[nodiscard]] operator ArrayRef<T>() const noexcept { return ArrayRef<T>(data(), m_size); }
    [[nodiscard]] operator MutArrayRef<T>() noexcept { return MutArrayRef<T>(data(), m_size); }

    [[nodiscard]] T* data() noexcept { return reinterpret_cast<T*>(m_storage); }
    [[nodiscard]] const T* data() const noexcept { return reinterpret_cast<const T*>(m_storage); }
    [[nodiscard]] isize size() const noexcept { return m_size; }
    [[nodiscard]] static constexpr isize capacity() noexcept { return N; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] T* begin() noexcept { return data(); }
    [[nodiscard]] const T* begin() const noexcept { return data(); }
    [[nodiscard]] T* end() noexcept { return data() + m_size; }
    [[nodiscard]] const T* end() const noexcept { return data() + m_size; }

    [[nodiscard]] T& operator[](isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        return data()[i];
    }

    [[nodiscard]] const T& operator[](isize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        return data()[i];
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
        KEEL_BOUNDS_ASSERT(m_size < N);
        T* slot = ::new (ptr_at(m_size)) T(forward<Args>(args)...);
        ++m_size;
        return *slot;
    }

    void push_back(const T& val) { emplace_back(val); }
    void push_back(T&& val) { emplace_back(move(val)); }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        if constexpr (!IsTriviallyDestructible<T>) {
            ptr_at(m_size)->~T();
        }
    }

    void clear() noexcept {
        if constexpr (!IsTriviallyDestructible<T>) {
            for (isize i = 0; i < m_size; ++i) {
                ptr_at(i)->~T();
            }
        }
        m_size = 0;
    }

    void remove_at(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        for (isize k = i; k < m_size - 1; ++k) {
            (*this)[k] = move((*this)[k + 1]);
        }
        pop_back();
    }

    void swap_remove(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        if (i != m_size - 1) {
            (*this)[i] = move((*this)[m_size - 1]);
        }
        pop_back();
    }

private:
    T* ptr_at(isize i) noexcept { return data() + i; }

    alignas(T) u8 m_storage[static_cast<usize>(N) * sizeof(T)];
    isize m_size = 0;
};

// Small-buffer optimized dynamic array: stores up to N elements inline, spills to Allocator when size > N.
template <typename T, isize N> class SmallArray {
    static_assert(N > 0, "SmallArray inline capacity N must be positive");
    static_assert(IsNothrowMoveConstructible<T>, "SmallArray<T, N> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "SmallArray<T, N> requires noexcept destructor");

public:
    explicit SmallArray(Allocator* alloc = nullptr) noexcept
        : m_data(inline_ptr()), m_size(0), m_cap(N), m_alloc(alloc ? alloc : default_allocator()) {}

    ~SmallArray() noexcept {
        clear();
        if (!is_inline()) {
            m_alloc->free_array(m_data, m_cap);
        }
    }

    SmallArray(const SmallArray&) = delete;
    SmallArray& operator=(const SmallArray&) = delete;

    SmallArray(SmallArray&& other) noexcept : m_data(inline_ptr()), m_size(0), m_cap(N), m_alloc(other.m_alloc) {
        move_from(other);
    }

    SmallArray& operator=(SmallArray&& other) noexcept {
        if (this != &other) {
            clear();
            if (!is_inline()) {
                m_alloc->free_array(m_data, m_cap);
                m_data = inline_ptr();
                m_cap = N;
            }
            m_alloc = other.m_alloc;
            move_from(other);
        }
        return *this;
    }

    [[nodiscard]] SmallArray clone(Allocator* alloc = nullptr) const {
        SmallArray copy(alloc ? alloc : m_alloc);
        copy.append(ArrayRef<T>(m_data, m_size));
        return copy;
    }

    [[nodiscard]] operator ArrayRef<T>() const noexcept { return ArrayRef<T>(m_data, m_size); }
    [[nodiscard]] operator MutArrayRef<T>() noexcept { return MutArrayRef<T>(m_data, m_size); }

    [[nodiscard]] T* data() noexcept { return m_data; }
    [[nodiscard]] const T* data() const noexcept { return m_data; }
    [[nodiscard]] isize size() const noexcept { return m_size; }
    [[nodiscard]] isize capacity() const noexcept { return m_cap; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool is_inline() const noexcept { return m_data == inline_ptr(); }

    [[nodiscard]] T* begin() noexcept { return m_data; }
    [[nodiscard]] const T* begin() const noexcept { return m_data; }
    [[nodiscard]] T* end() noexcept { return m_data + m_size; }
    [[nodiscard]] const T* end() const noexcept { return m_data + m_size; }

    [[nodiscard]] T& operator[](isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        return m_data[i];
    }

    [[nodiscard]] const T& operator[](isize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        return m_data[i];
    }

    void reserve(isize min_cap) {
        if (min_cap <= m_cap) return;
        isize new_cap = m_cap;
        while (new_cap < min_cap) new_cap *= 2;
        reallocate(new_cap);
    }

    void clear() noexcept {
        if constexpr (!IsTriviallyDestructible<T>) {
            for (isize i = 0; i < m_size; ++i) {
                m_data[i].~T();
            }
        }
        m_size = 0;
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
        if (m_size == m_cap) [[unlikely]] {
            reallocate(m_cap * 2);
        }
        T* slot = ::new (m_data + m_size) T(forward<Args>(args)...);
        ++m_size;
        return *slot;
    }

    void push_back(const T& val) { emplace_back(val); }
    void push_back(T&& val) { emplace_back(move(val)); }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        if constexpr (!IsTriviallyDestructible<T>) {
            m_data[m_size].~T();
        }
    }

    void append(ArrayRef<T> items) {
        const isize count = items.size();
        if (count == 0) return;
        reserve(m_size + count);
        for (isize i = 0; i < count; ++i) {
            ::new (m_data + m_size + i) T(items[i]);
        }
        m_size += count;
    }

    void remove_at(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        for (isize k = i; k < m_size - 1; ++k) {
            m_data[k] = move(m_data[k + 1]);
        }
        pop_back();
    }

    void swap_remove(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        if (i != m_size - 1) {
            m_data[i] = move(m_data[m_size - 1]);
        }
        pop_back();
    }

private:
    T* inline_ptr() noexcept { return reinterpret_cast<T*>(m_inline_storage); }
    const T* inline_ptr() const noexcept { return reinterpret_cast<const T*>(m_inline_storage); }

    void move_from(SmallArray& other) noexcept {
        if (other.is_inline()) {
            m_data = inline_ptr();
            m_cap = N;
            m_size = other.m_size;
            for (isize i = 0; i < m_size; ++i) {
                ::new (m_data + i) T(move(other.m_data[i]));
                if constexpr (!IsTriviallyDestructible<T>) other.m_data[i].~T();
            }
            other.m_size = 0;
        } else {
            m_data = other.m_data;
            m_size = other.m_size;
            m_cap = other.m_cap;
            other.m_data = other.inline_ptr();
            other.m_size = 0;
            other.m_cap = N;
        }
    }

    void reallocate(isize new_cap) {
        T* new_data = m_alloc->alloc_array<T>(new_cap);
        for (isize i = 0; i < m_size; ++i) {
            ::new (new_data + i) T(move(m_data[i]));
            if constexpr (!IsTriviallyDestructible<T>) m_data[i].~T();
        }
        if (!is_inline()) {
            m_alloc->free_array(m_data, m_cap);
        }
        m_data = new_data;
        m_cap = new_cap;
    }

    T* m_data = nullptr;
    isize m_size = 0;
    isize m_cap = N;
    Allocator* m_alloc = nullptr;
    alignas(T) u8 m_inline_storage[static_cast<usize>(N) * sizeof(T)];
};

} // namespace keel
