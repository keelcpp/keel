#pragma once

#include "keel/memory.h"

namespace keel {

template <typename T> struct Handle {
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] static constexpr Handle null() noexcept { return Handle{0, 0}; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return generation == 0; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return generation != 0; }

    [[nodiscard]] constexpr bool operator==(const Handle& other) const noexcept = default;
};

// Generational slot pool replacing shared/weak ownership without refcount overhead.
template <typename T> class Pool {
    static_assert(IsNothrowMoveConstructible<T>, "Pool<T> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "Pool<T> requires noexcept destructor");

public:
    explicit Pool(Allocator* alloc = nullptr) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    ~Pool() noexcept {
        clear();
        if (m_slots) {
            m_alloc->free_array(m_slots, m_cap);
        }
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    Pool(Pool&& other) noexcept
        : m_slots(other.m_slots), m_size(other.m_size), m_cap(other.m_cap), m_free_head(other.m_free_head),
          m_alloc(other.m_alloc) {
        other.m_slots = nullptr;
        other.m_size = 0;
        other.m_cap = 0;
        other.m_free_head = kInvalidSlot;
    }

    Pool& operator=(Pool&& other) noexcept {
        if (this != &other) {
            clear();
            if (m_slots) m_alloc->free_array(m_slots, m_cap);
            m_slots = other.m_slots;
            m_size = other.m_size;
            m_cap = other.m_cap;
            m_free_head = other.m_free_head;
            m_alloc = other.m_alloc;
            other.m_slots = nullptr;
            other.m_size = 0;
            other.m_cap = 0;
            other.m_free_head = kInvalidSlot;
        }
        return *this;
    }

    template <typename... Args> [[nodiscard]] Handle<T> emplace(Args&&... args) {
        if (m_free_head == kInvalidSlot) {
            grow(m_cap == 0 ? 8 : m_cap * 2);
        }
        const u32 idx = m_free_head;
        Slot& s = m_slots[idx];
        m_free_head = s.next_free;
        s.next_free = kInvalidSlot;
        ::new (s.storage) T(keel::forward<Args>(args)...);
        s.alive = true;
        if (s.generation == 0) s.generation = 1;
        ++m_size;
        return Handle<T>{idx, s.generation};
    }

    [[nodiscard]] Handle<T> insert(T val) { return emplace(keel::move(val)); }

    bool remove(Handle<T> h) noexcept {
        if (!is_valid(h)) return false;
        Slot& s = m_slots[h.index];
        s.ptr()->~T();
        s.alive = false;
        ++s.generation;
        if (s.generation == 0) s.generation = 1; // Skip 0 (reserved for null handle)
        s.next_free = m_free_head;
        m_free_head = h.index;
        --m_size;
        return true;
    }

    [[nodiscard]] bool is_valid(Handle<T> h) const noexcept {
        return h.generation != 0 && static_cast<usize>(h.index) < m_cap && m_slots[h.index].alive &&
               m_slots[h.index].generation == h.generation;
    }

    [[nodiscard]] T* get(Handle<T> h) noexcept { return is_valid(h) ? m_slots[h.index].ptr() : nullptr; }

    [[nodiscard]] const T* get(Handle<T> h) const noexcept { return is_valid(h) ? m_slots[h.index].ptr() : nullptr; }

    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    void clear() noexcept {
        for (usize i = 0; i < m_cap; ++i) {
            if (m_slots[i].alive) {
                m_slots[i].ptr()->~T();
                m_slots[i].alive = false;
                ++m_slots[i].generation;
                if (m_slots[i].generation == 0) m_slots[i].generation = 1;
            }
        }
        m_free_head = kInvalidSlot;
        for (usize i = m_cap; i > 0; --i) {
            m_slots[i - 1].next_free = m_free_head;
            m_free_head = static_cast<u32>(i - 1);
        }
        m_size = 0;
    }

private:
    static constexpr u32 kInvalidSlot = 0xFFFFFFFFu;

    struct Slot {
        alignas(T) u8 storage[sizeof(T)];
        u32 generation = 1;
        u32 next_free = kInvalidSlot;
        bool alive = false;

        T* ptr() noexcept { return reinterpret_cast<T*>(storage); }
        const T* ptr() const noexcept { return reinterpret_cast<const T*>(storage); }
    };

    void grow(usize new_cap) {
        Slot* new_slots = m_alloc->alloc_array<Slot>(new_cap);
        for (usize i = 0; i < m_cap; ++i) {
            new_slots[i].generation = m_slots[i].generation;
            new_slots[i].next_free = m_slots[i].next_free;
            new_slots[i].alive = m_slots[i].alive;
            if (m_slots[i].alive) {
                ::new (new_slots[i].storage) T(keel::move(*m_slots[i].ptr()));
                m_slots[i].ptr()->~T();
            }
        }
        for (usize i = m_cap; i < new_cap; ++i) {
            new_slots[i].generation = 1;
            new_slots[i].next_free = kInvalidSlot;
            new_slots[i].alive = false;
        }
        for (usize i = new_cap; i > m_cap; --i) {
            new_slots[i - 1].next_free = m_free_head;
            m_free_head = static_cast<u32>(i - 1);
        }
        if (m_slots) m_alloc->free_array(m_slots, m_cap);
        m_slots = new_slots;
        m_cap = new_cap;
    }

    Slot* m_slots = nullptr;
    usize m_size = 0;
    usize m_cap = 0;
    u32 m_free_head = kInvalidSlot;
    Allocator* m_alloc = nullptr;
};

} // namespace keel
