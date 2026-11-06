#pragma once

#include "keel/memory.h"
#include "keel/ref.h"

namespace keel {

// Move-only dynamic array. Pointers to elements may be invalidated on growth.
template <typename T> class Array {
    static_assert(IsNothrowMoveConstructible<T>, "Array<T> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "Array<T> requires noexcept destructor");

public:
    explicit Array(Allocator* alloc = nullptr) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    static Array with_capacity(isize cap, Allocator* alloc = nullptr) {
        Array arr(alloc);
        arr.reserve(cap);
        return arr;
    }

    static Array from(ArrayRef<T> items, Allocator* alloc = nullptr) {
        Array arr(alloc);
        arr.append(items);
        return arr;
    }

    ~Array() noexcept {
        clear();
        if (m_data) {
            m_alloc->free_array(m_data, m_cap);
        }
    }

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    Array(Array&& other) noexcept
        : m_data(other.m_data), m_size(other.m_size), m_cap(other.m_cap), m_alloc(other.m_alloc) {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_cap = 0;
    }

    Array& operator=(Array&& other) noexcept {
        if (this != &other) {
            clear();
            if (m_data) m_alloc->free_array(m_data, m_cap);
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

    [[nodiscard]] Array clone(Allocator* alloc = nullptr) const {
        Array copy(alloc ? alloc : m_alloc);
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
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

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

    [[nodiscard]] T& front() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] const T& front() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] T& back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    [[nodiscard]] const T& back() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    void reserve(isize min_cap) {
        if (min_cap <= m_cap) return;
        reallocate(min_cap);
    }

    void shrink_to_fit() {
        if (m_cap > m_size) {
            if (m_size == 0) {
                m_alloc->free_array(m_data, m_cap);
                m_data = nullptr;
                m_cap = 0;
            } else {
                reallocate(m_size);
            }
        }
    }

    void clear() noexcept {
        if constexpr (!IsTriviallyDestructible<T>) {
            for (isize i = 0; i < m_size; ++i) {
                m_data[i].~T();
            }
        }
        m_size = 0;
    }

    void resize(isize new_size) {
        KEEL_ASSERT(new_size >= 0);
        if (new_size < m_size) {
            if constexpr (!IsTriviallyDestructible<T>) {
                for (isize i = new_size; i < m_size; ++i) {
                    m_data[i].~T();
                }
            }
            m_size = new_size;
        } else if (new_size > m_size) {
            ensure_capacity(new_size);
            for (isize i = m_size; i < new_size; ++i) {
                ::new (m_data + i) T();
            }
            m_size = new_size;
        }
    }

    void resize(isize new_size, const T& fill_value) {
        KEEL_ASSERT(new_size >= 0);
        if (new_size < m_size) {
            if constexpr (!IsTriviallyDestructible<T>) {
                for (isize i = new_size; i < m_size; ++i) {
                    m_data[i].~T();
                }
            }
            m_size = new_size;
        } else if (new_size > m_size) {
            ensure_capacity(new_size);
            for (isize i = m_size; i < new_size; ++i) {
                ::new (m_data + i) T(fill_value);
            }
            m_size = new_size;
        }
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
        if (m_size == m_cap) [[unlikely]] {
            grow_for_one();
        }
        T* slot = ::new (m_data + m_size) T(keel::forward<Args>(args)...);
        ++m_size;
        return *slot;
    }

    void push_back(const T& val) {
        if (m_size == m_cap) [[unlikely]] {
            // Handle self-aliasing if val points inside m_data
            if (&val >= m_data && &val < m_data + m_size) {
                T copy(val);
                grow_for_one();
                ::new (m_data + m_size) T(keel::move(copy));
                ++m_size;
                return;
            }
            grow_for_one();
        }
        ::new (m_data + m_size) T(val);
        ++m_size;
    }

    void push_back(T&& val) {
        if (m_size == m_cap) [[unlikely]] {
            grow_for_one();
        }
        ::new (m_data + m_size) T(keel::move(val));
        ++m_size;
    }

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
        ensure_capacity(m_size + count);
        if constexpr (IsTriviallyCopyable<T>) {
            mem_copy(m_data + m_size, items.data(), static_cast<usize>(count) * sizeof(T));
        } else {
            for (isize i = 0; i < count; ++i) {
                ::new (m_data + m_size + i) T(items[i]);
            }
        }
        m_size += count;
    }

    // Removes element at index `i`, shifting subsequent elements left (O(N)).
    void remove_at(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        const isize tail = m_size - i - 1;
        if constexpr (IsTriviallyCopyable<T>) {
            if (tail > 0) {
                mem_move(m_data + i, m_data + i + 1, static_cast<usize>(tail) * sizeof(T));
            }
        } else {
            for (isize k = i; k < m_size - 1; ++k) {
                m_data[k] = keel::move(m_data[k + 1]);
            }
            m_data[m_size - 1].~T();
        }
        --m_size;
    }

    // Removes element at index `i` by moving the last element into its slot (O(1), does not preserve order).
    void swap_remove(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        --m_size;
        if (i != m_size) {
            m_data[i] = keel::move(m_data[m_size]);
        }
        if constexpr (!IsTriviallyDestructible<T>) {
            m_data[m_size].~T();
        }
    }

private:
    void grow_for_one() {
        const isize new_cap = (m_cap == 0) ? 8 : (m_cap * 2);
        reallocate(new_cap);
    }

    void ensure_capacity(isize needed) {
        if (needed <= m_cap) return;
        isize new_cap = (m_cap == 0) ? 8 : m_cap;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        reallocate(new_cap);
    }

    void reallocate(isize new_cap) {
        if constexpr (IsTriviallyCopyable<T>) {
            m_data = static_cast<T*>(m_alloc->realloc_bytes(
                m_data, m_cap * static_cast<isize>(sizeof(T)), new_cap * static_cast<isize>(sizeof(T)),
                static_cast<isize>(alignof(T))));
            m_cap = new_cap;
        } else {
            T* new_data = m_alloc->alloc_array<T>(new_cap);
            for (isize i = 0; i < m_size; ++i) {
                ::new (new_data + i) T(keel::move(m_data[i]));
                m_data[i].~T();
            }
            if (m_data) {
                m_alloc->free_array(m_data, m_cap);
            }
            m_data = new_data;
            m_cap = new_cap;
        }
    }

    T* m_data = nullptr;
    isize m_size = 0;
    isize m_cap = 0;
    Allocator* m_alloc = nullptr;
};

} // namespace keel
