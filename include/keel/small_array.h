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
            ::new (ptr_at(i)) T(keel::move(other[i]));
            if constexpr (!IsTriviallyDestructible<T>) other[i].~T();
        }
        other.m_size = 0;
    }

    InplaceArray& operator=(InplaceArray&& other) noexcept {
        if (this != &other) {
            clear();
            m_size = other.m_size;
            for (isize i = 0; i < m_size; ++i) {
                ::new (ptr_at(i)) T(keel::move(other[i]));
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
        T* slot = ::new (ptr_at(m_size)) T(keel::forward<Args>(args)...);
        ++m_size;
        return *slot;
    }

    void push_back(const T& val) { emplace_back(val); }
    void push_back(T&& val) { emplace_back(keel::move(val)); }

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
            (*this)[k] = keel::move((*this)[k + 1]);
        }
        pop_back();
    }

    void swap_remove(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        if (i != m_size - 1) {
            (*this)[i] = keel::move((*this)[m_size - 1]);
        }
        pop_back();
    }

private:
    T* ptr_at(isize i) noexcept { return data() + i; }

    alignas(T) u8 m_storage[static_cast<usize>(N) * sizeof(T)];
    isize m_size = 0;
};

// Small-buffer optimized dynamic array: stores up to N elements inline, spills to Allocator when size > N.
// Never stores a self-referential pointer to its inline buffer (`m_heap == nullptr` when inline).
template <typename T, isize N> class SmallArray {
    static_assert(N > 0, "SmallArray inline capacity N must be positive");
    static_assert(IsNothrowDestructible<T>, "SmallArray<T, N> requires noexcept destructor");

public:
    explicit SmallArray(Allocator* alloc = nullptr) noexcept
        : m_heap(nullptr), m_size(0), m_cap(N), m_alloc(alloc ? alloc : default_allocator()) {}

    ~SmallArray() noexcept {
        clear();
        if (m_heap) {
            m_alloc->free_array(m_heap, m_cap);
        }
    }

    SmallArray(const SmallArray&) = delete;
    SmallArray& operator=(const SmallArray&) = delete;

    SmallArray(SmallArray&& other) noexcept : m_heap(nullptr), m_size(0), m_cap(N), m_alloc(other.m_alloc) {
        move_from(other);
    }

    SmallArray& operator=(SmallArray&& other) noexcept {
        if (this != &other) {
            clear();
            if (m_heap) {
                m_alloc->free_array(m_heap, m_cap);
                m_heap = nullptr;
                m_cap = N;
            }
            m_alloc = other.m_alloc;
            move_from(other);
        }
        return *this;
    }

    [[nodiscard]] SmallArray clone(Allocator* alloc = nullptr) const {
        SmallArray copy(alloc ? alloc : m_alloc);
        copy.append(ArrayRef<T>(data(), m_size));
        return copy;
    }

    [[nodiscard]] operator ArrayRef<T>() const noexcept { return ArrayRef<T>(data(), m_size); }
    [[nodiscard]] operator MutArrayRef<T>() noexcept { return MutArrayRef<T>(data(), m_size); }

    [[nodiscard]] T* data() noexcept { return m_heap ? m_heap : inline_ptr(); }
    [[nodiscard]] const T* data() const noexcept { return m_heap ? m_heap : inline_ptr(); }
    [[nodiscard]] isize size() const noexcept { return m_size; }
    [[nodiscard]] isize capacity() const noexcept { return m_cap; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool is_inline() const noexcept { return m_heap == nullptr; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

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

    [[nodiscard]] T& front() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return data()[0];
    }

    [[nodiscard]] const T& front() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return data()[0];
    }

    [[nodiscard]] T& back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return data()[m_size - 1];
    }

    [[nodiscard]] const T& back() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return data()[m_size - 1];
    }

    void reserve(isize min_cap) {
        if (min_cap <= m_cap) return;
        isize new_cap = m_cap;
        while (new_cap < min_cap) new_cap *= 2;
        reallocate(new_cap);
    }

    void clear() noexcept {
        if constexpr (!IsTriviallyDestructible<T>) {
            T* cur = data();
            for (isize i = m_size; i > 0; --i) {
                cur[i - 1].~T();
            }
        }
        m_size = 0;
    }

    void resize(isize new_size) {
        KEEL_ASSERT(new_size >= 0);
        if (new_size < m_size) {
            if constexpr (!IsTriviallyDestructible<T>) {
                T* cur = data();
                for (isize i = m_size; i > new_size; --i) {
                    cur[i - 1].~T();
                }
            }
            m_size = new_size;
        } else if (new_size > m_size) {
            reserve(new_size);
            T* cur = data();
            for (isize i = m_size; i < new_size; ++i) {
                ::new (cur + i) T{};
            }
            m_size = new_size;
        }
    }

    void resize(isize new_size, const T& fill_value) {
        KEEL_ASSERT(new_size >= 0);
        if (new_size < m_size) {
            if constexpr (!IsTriviallyDestructible<T>) {
                T* cur = data();
                for (isize i = m_size; i > new_size; --i) {
                    cur[i - 1].~T();
                }
            }
            m_size = new_size;
        } else if (new_size > m_size) {
            reserve(new_size);
            T* cur = data();
            for (isize i = m_size; i < new_size; ++i) {
                ::new (cur + i) T(fill_value);
            }
            m_size = new_size;
        }
    }

    void assign(isize count, const T& fill_value) {
        KEEL_ASSERT(count >= 0);
        clear();
        reserve(count);
        T* cur = data();
        for (isize i = 0; i < count; ++i) {
            ::new (cur + i) T(fill_value);
        }
        m_size = count;
    }

    void assign(ArrayRef<T> items) {
        const T* cur = data();
        if (items.data() >= cur && items.data() < cur + m_size) {
            SmallArray tmp(m_alloc);
            tmp.append(items);
            *this = keel::move(tmp);
            return;
        }
        clear();
        append(items);
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
        if (m_size < m_cap) [[likely]] {
            T* slot = ::new (data() + m_size) T(keel::forward<Args>(args)...);
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
            data()[m_size].~T();
        }
    }

    void append(ArrayRef<T> items) {
        const isize count = items.size();
        if (count == 0) return;
        const T* cur = data();
        if (items.data() >= cur && items.data() < cur + m_size) {
            SmallArray tmp(m_alloc);
            tmp.append(items);
            reserve(m_size + count);
            T* dst = data();
            for (isize i = 0; i < count; ++i) {
                ::new (dst + m_size + i) T(keel::move(tmp[i]));
            }
            m_size += count;
            return;
        }
        reserve(m_size + count);
        T* dst = data();
        if constexpr (IsTriviallyCopyable<T>) {
            mem_copy(dst + m_size, items.data(), static_cast<usize>(count) * sizeof(T));
        } else {
            for (isize i = 0; i < count; ++i) {
                ::new (dst + m_size + i) T(items[i]);
            }
        }
        m_size += count;
    }

    void remove_at(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        T* cur = data();
        for (isize k = i; k < m_size - 1; ++k) {
            cur[k] = keel::move(cur[k + 1]);
        }
        pop_back();
    }

    void swap_remove(isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < m_size);
        T* cur = data();
        if (i != m_size - 1) {
            cur[i] = keel::move(cur[m_size - 1]);
        }
        pop_back();
    }

private:
    T* inline_ptr() noexcept { return reinterpret_cast<T*>(m_inline_storage); }
    const T* inline_ptr() const noexcept { return reinterpret_cast<const T*>(m_inline_storage); }

    static void relocate_elements(T* dst, T* src, isize count) {
        if (count <= 0) return;
        if constexpr (IsTriviallyCopyable<T>) {
            mem_copy(dst, src, static_cast<usize>(count) * sizeof(T));
        } else {
            for (isize i = 0; i < count; ++i) {
                ::new (dst + i) T(keel::move(src[i]));
                if constexpr (!IsTriviallyDestructible<T>) src[i].~T();
            }
        }
    }

    void move_from(SmallArray& other) noexcept {
        if (other.is_inline()) {
            m_heap = nullptr;
            m_cap = N;
            m_size = other.m_size;
            relocate_elements(inline_ptr(), other.inline_ptr(), m_size);
            other.m_size = 0;
        } else {
            m_heap = other.m_heap;
            m_size = other.m_size;
            m_cap = other.m_cap;
            other.m_heap = nullptr;
            other.m_size = 0;
            other.m_cap = N;
        }
    }

    void reallocate(isize new_cap) {
        T* new_data = m_alloc->alloc_array<T>(new_cap);
        T* old_data = data();
        relocate_elements(new_data, old_data, m_size);
        if (m_heap) {
            m_alloc->free_array(m_heap, m_cap);
        }
        m_heap = new_data;
        m_cap = new_cap;
    }

    template <typename... Args> T& emplace_back_slow(Args&&... args) {
        const isize new_cap = keel::max<isize>(m_cap * 2, N + 1);
        T* new_data = m_alloc->alloc_array<T>(new_cap);
        // Construct the new element before relocating/destroying existing elements so self-aliasing args stay valid
        T* slot = ::new (new_data + m_size) T(keel::forward<Args>(args)...);
        relocate_elements(new_data, data(), m_size);
        if (m_heap) {
            m_alloc->free_array(m_heap, m_cap);
        }
        m_heap = new_data;
        m_cap = new_cap;
        ++m_size;
        return *slot;
    }

    T* m_heap = nullptr;
    isize m_size = 0;
    isize m_cap = N;
    Allocator* m_alloc = nullptr;
    alignas(T) u8 m_inline_storage[static_cast<usize>(N) * sizeof(T)];
};

} // namespace keel
