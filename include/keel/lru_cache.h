#pragma once

#include "keel/array.h"
#include "keel/hash_map.h"

namespace keel {

// Flat O(1) Least-Recently-Used (LRU) cache with bounded capacity.
// Backed by a contiguous `Array` with intrusive index doubly-linked list and `HashMap` (zero node heap allocations).
template <typename K, typename V, isize Capacity = 64, typename HashFn = Hash<K>, typename EqualFn = Equal<K>>
class LruCache {
    static_assert(Capacity > 0, "LruCache Capacity must be positive");
    static constexpr u32 kNil = 0xFFFFFFFFu;

    struct Entry {
        K key{};
        V value{};
        u32 prev = kNil;
        u32 next = kNil;
    };

public:
    explicit LruCache(Allocator* alloc = nullptr) noexcept : m_entries(alloc), m_map(alloc) {}

    [[nodiscard]] V* get(const K& key) noexcept {
        const u32* idx_ptr = m_map.get(key);
        if (!idx_ptr) return nullptr;
        const u32 idx = *idx_ptr;
        move_to_front(idx);
        return &m_entries[static_cast<isize>(idx)].value;
    }

    [[nodiscard]] const V* find(const K& key) noexcept { return get(key); }

    V& put(const K& key, V value) {
        if (u32* idx_ptr = m_map.get(key)) {
            const u32 idx = *idx_ptr;
            m_entries[static_cast<isize>(idx)].value = keel::move(value);
            move_to_front(idx);
            return m_entries[static_cast<isize>(idx)].value;
        }

        u32 slot = kNil;
        if (m_entries.size() < Capacity) {
            if (m_entries.capacity() == 0) {
                m_entries.reserve(Capacity);
            }
            slot = static_cast<u32>(m_entries.size());
            m_entries.push_back(Entry{
                .key = key,
                .value = keel::move(value),
                .prev = kNil,
                .next = kNil,
            });
        } else {
            slot = m_tail;
            Entry& victim = m_entries[static_cast<isize>(slot)];
            (void)m_map.remove(victim.key);
            detach(slot);
            victim.key = key;
            victim.value = keel::move(value);
        }

        attach_front(slot);
        m_map.put(key, slot);
        return m_entries[static_cast<isize>(slot)].value;
    }

    void clear() noexcept {
        m_map.clear();
        m_entries.clear();
        m_head = kNil;
        m_tail = kNil;
    }

    [[nodiscard]] isize size() const noexcept { return m_map.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_map.empty(); }
    [[nodiscard]] static constexpr isize capacity() noexcept { return Capacity; }

private:
    void detach(u32 idx) noexcept {
        Entry& e = m_entries[static_cast<isize>(idx)];
        if (e.prev != kNil) {
            m_entries[static_cast<isize>(e.prev)].next = e.next;
        } else {
            m_head = e.next;
        }
        if (e.next != kNil) {
            m_entries[static_cast<isize>(e.next)].prev = e.prev;
        } else {
            m_tail = e.prev;
        }
        e.prev = kNil;
        e.next = kNil;
    }

    void attach_front(u32 idx) noexcept {
        Entry& e = m_entries[static_cast<isize>(idx)];
        e.prev = kNil;
        e.next = m_head;
        if (m_head != kNil) {
            m_entries[static_cast<isize>(m_head)].prev = idx;
        } else {
            m_tail = idx;
        }
        m_head = idx;
    }

    void move_to_front(u32 idx) noexcept {
        if (m_head == idx) return;
        detach(idx);
        attach_front(idx);
    }

    Array<Entry> m_entries;
    HashMap<K, u32, HashFn, EqualFn> m_map;
    u32 m_head = kNil;
    u32 m_tail = kNil;
};

} // namespace keel
