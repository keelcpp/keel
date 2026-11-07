#pragma once

#include "keel/bits.h"
#include "keel/memory.h"

namespace keel {

// Double-ended circular buffer (Deque / FIFO queue) backed by a single power-of-two allocation.
// Provides O(1) amortized push_front, push_back, pop_front, pop_back, and O(1) indexed access.
template <typename T> class RingBuffer {
    static_assert(IsNothrowMoveConstructible<T>, "RingBuffer<T> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "RingBuffer<T> requires noexcept destructor");

public:
    template <bool IsConst> class IteratorImpl {
    public:
        using BufferPtr = Conditional<IsConst, const RingBuffer*, RingBuffer*>;
        using RefType = Conditional<IsConst, const T&, T&>;
        using PtrType = Conditional<IsConst, const T*, T*>;

        constexpr IteratorImpl() noexcept = default;
        constexpr IteratorImpl(BufferPtr buf, usize index) noexcept : m_buf(buf), m_index(index) {}

        [[nodiscard]] constexpr RefType operator*() const noexcept { return (*m_buf)[m_index]; }
        [[nodiscard]] constexpr PtrType operator->() const noexcept { return &((*m_buf)[m_index]); }

        constexpr IteratorImpl& operator++() noexcept {
            ++m_index;
            return *this;
        }
        constexpr IteratorImpl operator++(int) noexcept {
            IteratorImpl tmp = *this;
            ++m_index;
            return tmp;
        }
        constexpr IteratorImpl& operator--() noexcept {
            --m_index;
            return *this;
        }
        constexpr IteratorImpl operator--(int) noexcept {
            IteratorImpl tmp = *this;
            --m_index;
            return tmp;
        }

        [[nodiscard]] constexpr bool operator==(const IteratorImpl& other) const noexcept {
            return m_index == other.m_index && m_buf == other.m_buf;
        }
        [[nodiscard]] constexpr bool operator!=(const IteratorImpl& other) const noexcept {
            return !(*this == other);
        }

    private:
        BufferPtr m_buf = nullptr;
        usize m_index = 0;
    };

    using Iterator = IteratorImpl<false>;
    using ConstIterator = IteratorImpl<true>;

    explicit RingBuffer(Allocator* alloc = nullptr) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    static RingBuffer with_capacity(usize cap, Allocator* alloc = nullptr) {
        RingBuffer rb(alloc);
        rb.reserve(cap);
        return rb;
    }

    ~RingBuffer() noexcept { reset(); }

    RingBuffer(const RingBuffer& other) : m_alloc(other.m_alloc) {
        if (other.m_size > 0) {
            reserve(other.m_size);
            for (usize i = 0; i < other.m_size; ++i) {
                ::new (m_data + i) T(other[i]);
            }
            m_head = 0;
            m_size = other.m_size;
        }
    }

    RingBuffer& operator=(const RingBuffer& other) {
        if (this != &other) {
            clear();
            if (other.m_size > 0) {
                reserve(other.m_size);
                for (usize i = 0; i < other.m_size; ++i) {
                    ::new (m_data + i) T(other[i]);
                }
                m_head = 0;
                m_size = other.m_size;
            }
        }
        return *this;
    }

    RingBuffer(RingBuffer&& other) noexcept
        : m_data(other.m_data),
          m_head(other.m_head),
          m_size(other.m_size),
          m_cap(other.m_cap),
          m_alloc(other.m_alloc) {
        other.m_data = nullptr;
        other.m_head = 0;
        other.m_size = 0;
        other.m_cap = 0;
    }

    RingBuffer& operator=(RingBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            m_data = other.m_data;
            m_head = other.m_head;
            m_size = other.m_size;
            m_cap = other.m_cap;
            m_alloc = other.m_alloc;
            other.m_data = nullptr;
            other.m_head = 0;
            other.m_size = 0;
            other.m_cap = 0;
        }
        return *this;
    }

    [[nodiscard]] RingBuffer clone(Allocator* alloc = nullptr) const {
        RingBuffer copy(alloc ? alloc : m_alloc);
        if (m_size > 0) {
            copy.reserve(m_size);
            for (usize i = 0; i < m_size; ++i) {
                ::new (copy.m_data + i) T((*this)[i]);
            }
            copy.m_head = 0;
            copy.m_size = m_size;
        }
        return copy;
    }

    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] usize capacity() const noexcept { return m_cap; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

    [[nodiscard]] Iterator begin() noexcept { return Iterator(this, 0); }
    [[nodiscard]] ConstIterator begin() const noexcept { return ConstIterator(this, 0); }
    [[nodiscard]] Iterator end() noexcept { return Iterator(this, m_size); }
    [[nodiscard]] ConstIterator end() const noexcept { return ConstIterator(this, m_size); }

    [[nodiscard]] T& operator[](usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return m_data[phys_index(i)];
    }

    [[nodiscard]] const T& operator[](usize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return m_data[phys_index(i)];
    }

    [[nodiscard]] T& front() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[m_head];
    }

    [[nodiscard]] const T& front() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[m_head];
    }

    [[nodiscard]] T& back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[phys_index(m_size - 1)];
    }

    [[nodiscard]] const T& back() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[phys_index(m_size - 1)];
    }

    void clear() noexcept {
        if constexpr (!IsTriviallyDestructible<T>) {
            for (usize i = 0; i < m_size; ++i) {
                m_data[phys_index(i)].~T();
            }
        }
        m_head = 0;
        m_size = 0;
    }

    void reset() noexcept {
        clear();
        if (m_data) {
            m_alloc->free_array(m_data, m_cap);
            m_data = nullptr;
            m_cap = 0;
        }
    }

    void reserve(usize min_cap) {
        if (min_cap <= m_cap) return;
        const usize pow2 = static_cast<usize>(bit_ceil(static_cast<u64>(min_cap < 8 ? 8 : min_cap)));
        reallocate(pow2);
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
        if (m_size == m_cap) [[unlikely]] {
            grow();
        }
        const usize tail = phys_index(m_size);
        T* slot = ::new (m_data + tail) T(keel::forward<Args>(args)...);
        ++m_size;
        return *slot;
    }

    void push_back(const T& val) {
        if (m_size == m_cap) [[unlikely]] {
            if (m_data && &val >= m_data && &val < m_data + m_cap) {
                T copy(val);
                grow();
                ::new (m_data + phys_index(m_size)) T(keel::move(copy));
                ++m_size;
                return;
            }
            grow();
        }
        ::new (m_data + phys_index(m_size)) T(val);
        ++m_size;
    }

    void push_back(T&& val) {
        if (m_size == m_cap) [[unlikely]] {
            grow();
        }
        ::new (m_data + phys_index(m_size)) T(keel::move(val));
        ++m_size;
    }

    template <typename... Args> T& emplace_front(Args&&... args) {
        if (m_size == m_cap) [[unlikely]] {
            grow();
        }
        const usize new_head = (m_head - 1) & (m_cap - 1);
        T* slot = ::new (m_data + new_head) T(keel::forward<Args>(args)...);
        m_head = new_head;
        ++m_size;
        return *slot;
    }

    void push_front(const T& val) {
        if (m_size == m_cap) [[unlikely]] {
            if (m_data && &val >= m_data && &val < m_data + m_cap) {
                T copy(val);
                grow();
                const usize new_head = (m_head - 1) & (m_cap - 1);
                ::new (m_data + new_head) T(keel::move(copy));
                m_head = new_head;
                ++m_size;
                return;
            }
            grow();
        }
        const usize new_head = (m_head - 1) & (m_cap - 1);
        ::new (m_data + new_head) T(val);
        m_head = new_head;
        ++m_size;
    }

    void push_front(T&& val) {
        if (m_size == m_cap) [[unlikely]] {
            grow();
        }
        const usize new_head = (m_head - 1) & (m_cap - 1);
        ::new (m_data + new_head) T(keel::move(val));
        m_head = new_head;
        ++m_size;
    }

    void pop_front() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        if constexpr (!IsTriviallyDestructible<T>) {
            m_data[m_head].~T();
        }
        m_head = (m_head + 1) & (m_cap - 1);
        --m_size;
        if (m_size == 0) {
            m_head = 0;
        }
    }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        if constexpr (!IsTriviallyDestructible<T>) {
            m_data[phys_index(m_size)].~T();
        }
        if (m_size == 0) {
            m_head = 0;
        }
    }

    [[nodiscard]] bool try_pop_front(T& out) noexcept {
        if (m_size == 0) return false;
        out = keel::move(m_data[m_head]);
        pop_front();
        return true;
    }

    [[nodiscard]] bool try_pop_back(T& out) noexcept {
        if (m_size == 0) return false;
        out = keel::move(m_data[phys_index(m_size - 1)]);
        pop_back();
        return true;
    }

private:
    [[nodiscard]] constexpr usize phys_index(usize logical) const noexcept {
        return (m_head + logical) & (m_cap - 1);
    }

    void grow() {
        const usize new_cap = (m_cap == 0) ? 8 : (m_cap * 2);
        reallocate(new_cap);
    }

    void reallocate(usize new_cap) {
        T* new_data = m_alloc->alloc_array<T>(new_cap);
        for (usize i = 0; i < m_size; ++i) {
            const usize old_idx = phys_index(i);
            ::new (new_data + i) T(keel::move(m_data[old_idx]));
            if constexpr (!IsTriviallyDestructible<T>) {
                m_data[old_idx].~T();
            }
        }
        if (m_data) {
            m_alloc->free_array(m_data, m_cap);
        }
        m_data = new_data;
        m_head = 0;
        m_cap = new_cap;
    }

    T* m_data = nullptr;
    usize m_head = 0;
    usize m_size = 0;
    usize m_cap = 0;
    Allocator* m_alloc = nullptr;
};

template <typename T> using Deque = RingBuffer<T>;

} // namespace keel
