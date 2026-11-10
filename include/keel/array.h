#pragma once

#include "keel/memory.h"
#include "keel/ref.h"
#include "keel/small_array.h"

namespace keel {

// Move-only dynamic array. Pointers to elements may be invalidated on growth.
template <typename T> class Array {
    static_assert(IsNothrowMoveConstructible<T>, "Array<T> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "Array<T> requires noexcept destructor");

public:
    explicit Array(Allocator* alloc = nullptr) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    explicit Array(usize count, Allocator* alloc = nullptr) : m_alloc(alloc ? alloc : default_allocator()) {
        resize(count);
    }

    Array(usize count, const T& fill_value, Allocator* alloc = nullptr)
        : m_alloc(alloc ? alloc : default_allocator()) {
        assign(count, fill_value);
    }

    explicit Array(ArrayRef<T> items, Allocator* alloc = nullptr)
        : m_alloc(alloc ? alloc : default_allocator()) {
        append(items);
    }

    [[nodiscard]] static Array with_capacity(usize cap, Allocator* alloc = nullptr) {
        Array arr(alloc);
        arr.reserve(cap);
        return arr;
    }

    [[nodiscard]] static Array from(ArrayRef<T> items, Allocator* alloc = nullptr) {
        return Array(items, alloc);
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
        Allocator* dst_alloc = alloc ? alloc : m_alloc;
        Array copy(dst_alloc);
        if (m_size > 0) {
            copy.reserve(m_size);
            if constexpr (IsTriviallyCopyable<T>) {
                mem_copy(copy.m_data, m_data, m_size * sizeof(T));
                copy.m_size = m_size;
            } else {
                for (usize i = 0; i < m_size; ++i) {
                    copy.push_back(detail::clone_value(m_data[i], dst_alloc));
                }
            }
        }
        return copy;
    }

    [[nodiscard]] operator ArrayRef<T>() const noexcept { return ArrayRef<T>(m_data, m_size); }
    [[nodiscard]] operator MutArrayRef<T>() noexcept { return MutArrayRef<T>(m_data, m_size); }
    [[nodiscard]] ArrayRef<T> as_ref() const noexcept { return ArrayRef<T>(m_data, m_size); }
    [[nodiscard]] MutArrayRef<T> as_mut_ref() noexcept { return MutArrayRef<T>(m_data, m_size); }
    [[nodiscard]] ArrayRef<T> slice(usize offset, usize count = kInvalidIndex) const noexcept {
        return as_ref().slice(offset, count);
    }

    [[nodiscard]] T* data() noexcept { return m_data; }
    [[nodiscard]] const T* data() const noexcept { return m_data; }
    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] usize capacity() const noexcept { return m_cap; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

    [[nodiscard]] T* begin() noexcept { return m_data; }
    [[nodiscard]] const T* begin() const noexcept { return m_data; }
    [[nodiscard]] T* end() noexcept { return m_data + m_size; }
    [[nodiscard]] const T* end() const noexcept { return m_data + m_size; }

    [[nodiscard]] T& operator[](usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return m_data[i];
    }

    [[nodiscard]] const T& operator[](usize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
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

    void reserve(usize min_cap) {
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
            for (usize i = m_size; i > 0; --i) {
                m_data[i - 1].~T();
            }
        }
        m_size = 0;
    }

    void resize(usize new_size) {
        if (new_size < m_size) {
            if constexpr (!IsTriviallyDestructible<T>) {
                for (usize i = m_size; i > new_size; --i) {
                    m_data[i - 1].~T();
                }
            }
            m_size = new_size;
        } else if (new_size > m_size) {
            ensure_capacity(new_size);
            for (usize i = m_size; i < new_size; ++i) {
                ::new (m_data + i) T{};
            }
            m_size = new_size;
        }
    }

    void resize(usize new_size, const T& fill_value) {
        if (new_size < m_size) {
            if constexpr (!IsTriviallyDestructible<T>) {
                for (usize i = m_size; i > new_size; --i) {
                    m_data[i - 1].~T();
                }
            }
            m_size = new_size;
        } else if (new_size > m_size) {
            if (new_size > m_cap && m_data && &fill_value >= m_data && &fill_value < m_data + m_size) {
                T fill_copy(fill_value);
                ensure_capacity(new_size);
                for (usize i = m_size; i < new_size; ++i) {
                    ::new (m_data + i) T(fill_copy);
                }
                m_size = new_size;
                return;
            }
            ensure_capacity(new_size);
            for (usize i = m_size; i < new_size; ++i) {
                ::new (m_data + i) T(fill_value);
            }
            m_size = new_size;
        }
    }

    void assign(usize count, const T& fill_value) {
        if (m_data && &fill_value >= m_data && &fill_value < m_data + m_size) {
            T fill_copy(fill_value);
            clear();
            ensure_capacity(count);
            for (usize i = 0; i < count; ++i) {
                ::new (m_data + i) T(fill_copy);
            }
            m_size = count;
            return;
        }
        clear();
        ensure_capacity(count);
        for (usize i = 0; i < count; ++i) {
            ::new (m_data + i) T(fill_value);
        }
        m_size = count;
    }

    void assign(ArrayRef<T> items) {
        if (m_data && items.data() >= m_data && items.data() < m_data + m_size) {
            Array tmp(m_alloc);
            tmp.append(items);
            *this = keel::move(tmp);
            return;
        }
        clear();
        append(items);
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
        if (m_size < m_cap) [[likely]] {
            T* slot = ::new (m_data + m_size) T(keel::forward<Args>(args)...);
            ++m_size;
            return *slot;
        }
        return emplace_back_slow(keel::forward<Args>(args)...);
    }

    void push_back(const T& val) { emplace_back(val); }
    void push_back(T&& val) { emplace_back(keel::move(val)); }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        if constexpr (!IsTriviallyDestructible<T>) {
            m_data[m_size].~T();
        }
    }

    void append(ArrayRef<T> items) {
        const usize count = items.size();
        if (count == 0) return;
        if (m_data && items.data() >= m_data && items.data() < m_data + m_size) {
            const usize offset = static_cast<usize>(items.data() - m_data);
            ensure_capacity(m_size + count);
            if constexpr (IsTriviallyCopyable<T>) {
                mem_copy(m_data + m_size, m_data + offset, count * sizeof(T));
            } else {
                for (usize i = 0; i < count; ++i) {
                    ::new (m_data + m_size + i) T(m_data[offset + i]);
                }
            }
            m_size += count;
            return;
        }
        ensure_capacity(m_size + count);
        if constexpr (IsTriviallyCopyable<T>) {
            mem_copy(m_data + m_size, items.data(), count * sizeof(T));
        } else {
            for (usize i = 0; i < count; ++i) {
                ::new (m_data + m_size + i) T(items[i]);
            }
        }
        m_size += count;
    }

    // Removes element at index `i`, shifting subsequent elements left (O(N)).
    void remove_at(usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        const usize tail = m_size - i - 1;
        if constexpr (IsTriviallyCopyable<T>) {
            if (tail > 0) {
                mem_move(m_data + i, m_data + i + 1, tail * sizeof(T));
            }
        } else {
            for (usize k = i; k < m_size - 1; ++k) {
                m_data[k] = keel::move(m_data[k + 1]);
            }
            m_data[m_size - 1].~T();
        }
        --m_size;
    }

    void erase(usize i) noexcept { remove_at(i); }

    // Removes element at index `i` by moving the last element into its slot (O(1), does not preserve order).
    void swap_remove(usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        --m_size;
        if (i != m_size) {
            m_data[i] = keel::move(m_data[m_size]);
        }
        if constexpr (!IsTriviallyDestructible<T>) {
            m_data[m_size].~T();
        }
    }

    [[nodiscard]] friend bool operator==(const Array& a, const Array& b) noexcept
        requires requires(const T& x) {
            { x == x } -> detail::ConvertibleTo<bool>;
        }
    {
        return a.as_ref() == b.as_ref();
    }

    [[nodiscard]] friend bool operator==(const Array& a, ArrayRef<T> b) noexcept
        requires requires(const T& x) {
            { x == x } -> detail::ConvertibleTo<bool>;
        }
    {
        return a.as_ref() == b;
    }

private:
    template <typename... Args> T& emplace_back_slow(Args&&... args) {
        const usize new_cap = (m_cap == 0) ? 8 : (m_cap * 2);
        T* new_data = m_alloc->alloc_array<T>(new_cap);
        // Construct the new element before relocating/destroying existing elements so self-aliasing args stay valid
        T* slot = ::new (new_data + m_size) T(keel::forward<Args>(args)...);
        if (m_size > 0 && m_data) {
            if constexpr (IsTriviallyCopyable<T>) {
                mem_copy(new_data, m_data, m_size * sizeof(T));
            } else {
                for (usize i = 0; i < m_size; ++i) {
                    ::new (new_data + i) T(keel::move(m_data[i]));
                    m_data[i].~T();
                }
            }
        }
        if (m_data) {
            m_alloc->free_array(m_data, m_cap);
        }
        m_data = new_data;
        m_cap = new_cap;
        ++m_size;
        return *slot;
    }

    void ensure_capacity(usize needed) {
        if (needed <= m_cap) return;
        usize new_cap = (m_cap == 0) ? 8 : m_cap;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        reallocate(new_cap);
    }

    void reallocate(usize new_cap) {
        if constexpr (IsTriviallyCopyable<T>) {
            m_data =
                static_cast<T*>(m_alloc->realloc_bytes(m_data, m_cap * sizeof(T), new_cap * sizeof(T), alignof(T)));
            m_cap = new_cap;
        } else {
            T* new_data = m_alloc->alloc_array<T>(new_cap);
            for (usize i = 0; i < m_size; ++i) {
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
    usize m_size = 0;
    usize m_cap = 0;
    Allocator* m_alloc = nullptr;
};

} // namespace keel
