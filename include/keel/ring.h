#pragma once

#include "keel/bits.h"
#include "keel/memory.h"

namespace keel {

namespace detail {

template <typename RingType, typename T, bool IsConst> class RingIterator {
public:
    using RingPtr = Conditional<IsConst, const RingType*, RingType*>;
    using RefType = Conditional<IsConst, const T&, T&>;
    using PtrType = Conditional<IsConst, const T*, T*>;

    constexpr RingIterator() noexcept = default;
    constexpr RingIterator(RingPtr ring, usize index) noexcept : m_ring(ring), m_index(index) {}

    [[nodiscard]] constexpr RefType operator*() const noexcept { return (*m_ring)[m_index]; }
    [[nodiscard]] constexpr PtrType operator->() const noexcept { return &((*m_ring)[m_index]); }

    constexpr RingIterator& operator++() noexcept {
        ++m_index;
        return *this;
    }
    constexpr RingIterator operator++(int) noexcept {
        RingIterator tmp = *this;
        ++m_index;
        return tmp;
    }
    constexpr RingIterator& operator--() noexcept {
        --m_index;
        return *this;
    }
    constexpr RingIterator operator--(int) noexcept {
        RingIterator tmp = *this;
        --m_index;
        return tmp;
    }

    [[nodiscard]] constexpr bool operator==(const RingIterator& other) const noexcept {
        return m_index == other.m_index && m_ring == other.m_ring;
    }
    [[nodiscard]] constexpr bool operator!=(const RingIterator& other) const noexcept { return !(*this == other); }

private:
    RingPtr m_ring = nullptr;
    usize m_index = 0;
};

} // namespace detail

// Fixed-capacity inline double-ended circular buffer with zero heap allocations.
template <typename T, usize N> class FixedRing {
    static_assert(N > 0, "FixedRing capacity N must be positive");
    static_assert(IsNothrowMoveConstructible<T>, "FixedRing<T, N> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "FixedRing<T, N> requires noexcept destructor");

public:
    using Iterator = detail::RingIterator<FixedRing, T, false>;
    using ConstIterator = detail::RingIterator<FixedRing, T, true>;

    FixedRing() noexcept = default;
    ~FixedRing() noexcept { clear(); }

    FixedRing(const FixedRing&) = delete;
    FixedRing& operator=(const FixedRing&) = delete;

    FixedRing(FixedRing&& other) noexcept : m_head(0), m_size(other.m_size) {
        for (usize i = 0; i < m_size; ++i) {
            T& src = other[i];
            ::new (slot_ptr(i)) T(keel::move(src));
            if constexpr (!IsTriviallyDestructible<T>) src.~T();
        }
        other.m_head = 0;
        other.m_size = 0;
    }

    FixedRing& operator=(FixedRing&& other) noexcept {
        if (this != &other) {
            clear();
            m_head = 0;
            m_size = other.m_size;
            for (usize i = 0; i < m_size; ++i) {
                T& src = other[i];
                ::new (slot_ptr(i)) T(keel::move(src));
                if constexpr (!IsTriviallyDestructible<T>) src.~T();
            }
            other.m_head = 0;
            other.m_size = 0;
        }
        return *this;
    }

    [[nodiscard]] FixedRing clone() const {
        FixedRing copy;
        for (usize i = 0; i < m_size; ++i) {
            copy.push_back((*this)[i]);
        }
        return copy;
    }

    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] static constexpr usize capacity() noexcept { return N; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool full() const noexcept { return m_size == N; }

    [[nodiscard]] Iterator begin() noexcept { return Iterator(this, 0); }
    [[nodiscard]] ConstIterator begin() const noexcept { return ConstIterator(this, 0); }
    [[nodiscard]] Iterator end() noexcept { return Iterator(this, m_size); }
    [[nodiscard]] ConstIterator end() const noexcept { return ConstIterator(this, m_size); }

    [[nodiscard]] T& operator[](usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return *slot_ptr(phys_index(i));
    }

    [[nodiscard]] const T& operator[](usize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return *slot_ptr(phys_index(i));
    }

    [[nodiscard]] T& front() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return *slot_ptr(m_head);
    }

    [[nodiscard]] const T& front() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return *slot_ptr(m_head);
    }

    [[nodiscard]] T& back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return *slot_ptr(phys_index(m_size - 1));
    }

    [[nodiscard]] const T& back() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return *slot_ptr(phys_index(m_size - 1));
    }

    void clear() noexcept {
        if constexpr (!IsTriviallyDestructible<T>) {
            for (usize i = 0; i < m_size; ++i) {
                slot_ptr(phys_index(i))->~T();
            }
        }
        m_head = 0;
        m_size = 0;
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
        KEEL_BOUNDS_ASSERT(m_size < N);
        const usize tail = phys_index(m_size);
        T* slot = ::new (slot_ptr(tail)) T(keel::forward<Args>(args)...);
        ++m_size;
        return *slot;
    }

    void push_back(const T& val) { emplace_back(val); }
    void push_back(T&& val) { emplace_back(keel::move(val)); }

    [[nodiscard]] bool try_push_back(const T& val) {
        if (m_size >= N) return false;
        emplace_back(val);
        return true;
    }

    [[nodiscard]] bool try_push_back(T&& val) {
        if (m_size >= N) return false;
        emplace_back(keel::move(val));
        return true;
    }

    // Appends to the back, overwriting the oldest (front) element when full.
    void push_back_overwrite(T val) {
        if (m_size == N) {
            pop_front();
        }
        emplace_back(keel::move(val));
    }

    template <typename... Args> T& emplace_front(Args&&... args) {
        KEEL_BOUNDS_ASSERT(m_size < N);
        const usize new_head = step_back(m_head);
        T* slot = ::new (slot_ptr(new_head)) T(keel::forward<Args>(args)...);
        m_head = new_head;
        ++m_size;
        return *slot;
    }

    void push_front(const T& val) { emplace_front(val); }
    void push_front(T&& val) { emplace_front(keel::move(val)); }

    void pop_front() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        if constexpr (!IsTriviallyDestructible<T>) {
            slot_ptr(m_head)->~T();
        }
        m_head = step_forward(m_head);
        --m_size;
        if (m_size == 0) m_head = 0;
    }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        if constexpr (!IsTriviallyDestructible<T>) {
            slot_ptr(phys_index(m_size))->~T();
        }
        if (m_size == 0) m_head = 0;
    }

    [[nodiscard]] bool try_pop_front(T& out) noexcept {
        if (m_size == 0) return false;
        out = keel::move(*slot_ptr(m_head));
        pop_front();
        return true;
    }

    [[nodiscard]] bool try_pop_back(T& out) noexcept {
        if (m_size == 0) return false;
        out = keel::move(*slot_ptr(phys_index(m_size - 1)));
        pop_back();
        return true;
    }

private:
    static constexpr bool kPow2 = is_power_of_two(static_cast<u64>(N));

    [[nodiscard]] constexpr usize phys_index(usize logical) const noexcept {
        if constexpr (kPow2) {
            return (m_head + logical) & (N - 1);
        } else {
            const usize idx = m_head + logical;
            return (idx >= N) ? (idx - N) : idx;
        }
    }

    [[nodiscard]] static constexpr usize step_forward(usize idx) noexcept {
        if constexpr (kPow2) {
            return (idx + 1) & (N - 1);
        } else {
            return (idx + 1 == N) ? 0 : (idx + 1);
        }
    }

    [[nodiscard]] static constexpr usize step_back(usize idx) noexcept {
        if constexpr (kPow2) {
            return (idx - 1) & (N - 1);
        } else {
            return (idx == 0) ? (N - 1) : (idx - 1);
        }
    }

    [[nodiscard]] T* slot_ptr(usize phys) noexcept { return reinterpret_cast<T*>(m_storage) + phys; }
    [[nodiscard]] const T* slot_ptr(usize phys) const noexcept { return reinterpret_cast<const T*>(m_storage) + phys; }

    usize m_head = 0;
    usize m_size = 0;
    alignas(T) u8 m_storage[N * sizeof(T)];
};

// Small-buffer optimized double-ended circular buffer: stores up to N elements inline,
// and spills to Allocator when size > N. Never stores a self-referential pointer (`m_heap == nullptr` when inline).
template <typename T, usize N> class SmallRing {
    static_assert(N > 0, "SmallRing inline capacity N must be positive");
    static_assert(IsNothrowMoveConstructible<T>, "SmallRing<T, N> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "SmallRing<T, N> requires noexcept destructor");

public:
    using Iterator = detail::RingIterator<SmallRing, T, false>;
    using ConstIterator = detail::RingIterator<SmallRing, T, true>;

    explicit SmallRing(Allocator* alloc = nullptr) noexcept
        : m_heap(nullptr), m_head(0), m_size(0), m_cap(N), m_alloc(alloc ? alloc : default_allocator()) {}

    ~SmallRing() noexcept {
        clear();
        if (m_heap) {
            m_alloc->free_array(m_heap, m_cap);
        }
    }

    SmallRing(const SmallRing&) = delete;
    SmallRing& operator=(const SmallRing&) = delete;

    SmallRing(SmallRing&& other) noexcept
        : m_heap(nullptr), m_head(0), m_size(0), m_cap(N), m_alloc(other.m_alloc) {
        move_from(other);
    }

    SmallRing& operator=(SmallRing&& other) noexcept {
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

    [[nodiscard]] SmallRing clone(Allocator* alloc = nullptr) const {
        SmallRing copy(alloc ? alloc : m_alloc);
        copy.reserve(m_size);
        for (usize i = 0; i < m_size; ++i) {
            copy.push_back((*this)[i]);
        }
        return copy;
    }

    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] usize capacity() const noexcept { return m_cap; }
    [[nodiscard]] static constexpr usize inline_capacity() noexcept { return N; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool is_inline() const noexcept { return m_heap == nullptr; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

    [[nodiscard]] Iterator begin() noexcept { return Iterator(this, 0); }
    [[nodiscard]] ConstIterator begin() const noexcept { return ConstIterator(this, 0); }
    [[nodiscard]] Iterator end() noexcept { return Iterator(this, m_size); }
    [[nodiscard]] ConstIterator end() const noexcept { return ConstIterator(this, m_size); }

    [[nodiscard]] T& operator[](usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return buf_ptr()[phys_index(i)];
    }

    [[nodiscard]] const T& operator[](usize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i < m_size);
        return buf_ptr()[phys_index(i)];
    }

    [[nodiscard]] T& front() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return buf_ptr()[m_head];
    }

    [[nodiscard]] const T& front() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return buf_ptr()[m_head];
    }

    [[nodiscard]] T& back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return buf_ptr()[phys_index(m_size - 1)];
    }

    [[nodiscard]] const T& back() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return buf_ptr()[phys_index(m_size - 1)];
    }

    void clear() noexcept {
        if constexpr (!IsTriviallyDestructible<T>) {
            T* cur = buf_ptr();
            for (usize i = 0; i < m_size; ++i) {
                cur[phys_index(i)].~T();
            }
        }
        m_head = 0;
        m_size = 0;
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
        T* slot = ::new (buf_ptr() + tail) T(keel::forward<Args>(args)...);
        ++m_size;
        return *slot;
    }

    void push_back(const T& val) {
        if (m_size == m_cap) [[unlikely]] {
            const T* cur = buf_ptr();
            if (&val >= cur && &val < cur + m_cap) {
                T copy(val);
                grow();
                ::new (buf_ptr() + phys_index(m_size)) T(keel::move(copy));
                ++m_size;
                return;
            }
            grow();
        }
        ::new (buf_ptr() + phys_index(m_size)) T(val);
        ++m_size;
    }

    void push_back(T&& val) { emplace_back(keel::move(val)); }

    template <typename... Args> T& emplace_front(Args&&... args) {
        if (m_size == m_cap) [[unlikely]] {
            grow();
        }
        const usize new_head = (m_head == 0) ? (m_cap - 1) : (m_head - 1);
        T* slot = ::new (buf_ptr() + new_head) T(keel::forward<Args>(args)...);
        m_head = new_head;
        ++m_size;
        return *slot;
    }

    void push_front(const T& val) {
        if (m_size == m_cap) [[unlikely]] {
            const T* cur = buf_ptr();
            if (&val >= cur && &val < cur + m_cap) {
                T copy(val);
                grow();
                const usize new_head = (m_head == 0) ? (m_cap - 1) : (m_head - 1);
                ::new (buf_ptr() + new_head) T(keel::move(copy));
                m_head = new_head;
                ++m_size;
                return;
            }
            grow();
        }
        const usize new_head = (m_head == 0) ? (m_cap - 1) : (m_head - 1);
        ::new (buf_ptr() + new_head) T(val);
        m_head = new_head;
        ++m_size;
    }

    void push_front(T&& val) { emplace_front(keel::move(val)); }

    void pop_front() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        if constexpr (!IsTriviallyDestructible<T>) {
            buf_ptr()[m_head].~T();
        }
        m_head = (m_head + 1 == m_cap) ? 0 : (m_head + 1);
        --m_size;
        if (m_size == 0) m_head = 0;
    }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        if constexpr (!IsTriviallyDestructible<T>) {
            buf_ptr()[phys_index(m_size)].~T();
        }
        if (m_size == 0) m_head = 0;
    }

    [[nodiscard]] bool try_pop_front(T& out) noexcept {
        if (m_size == 0) return false;
        out = keel::move(buf_ptr()[m_head]);
        pop_front();
        return true;
    }

    [[nodiscard]] bool try_pop_back(T& out) noexcept {
        if (m_size == 0) return false;
        out = keel::move(buf_ptr()[phys_index(m_size - 1)]);
        pop_back();
        return true;
    }

private:
    [[nodiscard]] T* inline_ptr() noexcept { return reinterpret_cast<T*>(m_inline_storage); }
    [[nodiscard]] const T* inline_ptr() const noexcept { return reinterpret_cast<const T*>(m_inline_storage); }
    [[nodiscard]] T* buf_ptr() noexcept { return m_heap ? m_heap : inline_ptr(); }
    [[nodiscard]] const T* buf_ptr() const noexcept { return m_heap ? m_heap : inline_ptr(); }

    [[nodiscard]] constexpr usize phys_index(usize logical) const noexcept {
        const usize idx = m_head + logical;
        return (idx >= m_cap) ? (idx - m_cap) : idx;
    }

    void move_from(SmallRing& other) noexcept {
        if (other.is_inline()) {
            m_heap = nullptr;
            m_cap = N;
            m_head = 0;
            m_size = other.m_size;
            T* dst = inline_ptr();
            for (usize i = 0; i < m_size; ++i) {
                T& src = other[i];
                ::new (dst + i) T(keel::move(src));
                if constexpr (!IsTriviallyDestructible<T>) src.~T();
            }
            other.m_head = 0;
            other.m_size = 0;
        } else {
            m_heap = other.m_heap;
            m_head = other.m_head;
            m_size = other.m_size;
            m_cap = other.m_cap;
            other.m_heap = nullptr;
            other.m_head = 0;
            other.m_size = 0;
            other.m_cap = N;
        }
    }

    void grow() {
        const usize min_next = keel::max<usize>(m_cap * 2, 8);
        const usize new_cap = static_cast<usize>(bit_ceil(static_cast<u64>(min_next)));
        reallocate(new_cap);
    }

    void reallocate(usize new_cap) {
        T* new_data = m_alloc->alloc_array<T>(new_cap);
        T* cur = buf_ptr();
        for (usize i = 0; i < m_size; ++i) {
            const usize old_idx = phys_index(i);
            ::new (new_data + i) T(keel::move(cur[old_idx]));
            if constexpr (!IsTriviallyDestructible<T>) {
                cur[old_idx].~T();
            }
        }
        if (m_heap) {
            m_alloc->free_array(m_heap, m_cap);
        }
        m_heap = new_data;
        m_head = 0;
        m_cap = new_cap;
    }

    T* m_heap = nullptr;
    usize m_head = 0;
    usize m_size = 0;
    usize m_cap = N;
    Allocator* m_alloc = nullptr;
    alignas(T) u8 m_inline_storage[N * sizeof(T)];
};

// Move-only double-ended circular buffer (Deque / FIFO queue) backed by a power-of-two allocation.
// Provides O(1) amortized push_front, push_back, pop_front, pop_back, and O(1) indexed access.
template <typename T> class Ring {
    static_assert(IsNothrowMoveConstructible<T>, "Ring<T> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "Ring<T> requires noexcept destructor");

public:
    using Iterator = detail::RingIterator<Ring, T, false>;
    using ConstIterator = detail::RingIterator<Ring, T, true>;

    explicit Ring(Allocator* alloc = nullptr) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    static Ring with_capacity(usize cap, Allocator* alloc = nullptr) {
        Ring rb(alloc);
        rb.reserve(cap);
        return rb;
    }

    ~Ring() noexcept { reset(); }

    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;

    Ring(Ring&& other) noexcept
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

    Ring& operator=(Ring&& other) noexcept {
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

    [[nodiscard]] Ring clone(Allocator* alloc = nullptr) const {
        Ring copy(alloc ? alloc : m_alloc);
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

template <typename T> using Deque = Ring<T>;

} // namespace keel
