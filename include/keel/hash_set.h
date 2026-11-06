#pragma once

#include "keel/bits.h"
#include "keel/memory.h"
#include "keel/ref.h"

namespace keel {

// Open-addressed hash set with flat control-byte metadata and flat key array.
template <typename K, typename HashFn = Hash<K>, typename EqualFn = Equal<K>> class HashSet {
    static_assert(IsNothrowMoveConstructible<K> && IsNothrowDestructible<K>,
                  "HashSet key type K requires noexcept move and destructor");

    static constexpr u8 kCtrlEmpty = 0x00;
    static constexpr u8 kCtrlDeleted = 0x01;

    [[nodiscard]] static constexpr u8 make_tag(u64 h) noexcept { return static_cast<u8>(0x80u | (h >> 57)); }
    [[nodiscard]] static constexpr bool is_occupied(u8 ctrl) noexcept { return (ctrl & 0x80u) != 0; }

public:
    class ConstIterator {
    public:
        constexpr ConstIterator(const u8* ctrl, const K* keys, isize idx, isize cap) noexcept
            : m_ctrl(ctrl), m_keys(keys), m_idx(idx), m_cap(cap) {
            advance_to_occupied();
        }

        [[nodiscard]] constexpr const K& operator*() const noexcept { return m_keys[m_idx]; }

        constexpr ConstIterator& operator++() noexcept {
            ++m_idx;
            advance_to_occupied();
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const ConstIterator& other) const noexcept {
            return m_idx == other.m_idx;
        }
        [[nodiscard]] constexpr bool operator!=(const ConstIterator& other) const noexcept {
            return m_idx != other.m_idx;
        }

    private:
        constexpr void advance_to_occupied() noexcept {
            while (m_idx < m_cap && !is_occupied(m_ctrl[m_idx])) {
                ++m_idx;
            }
        }

        const u8* m_ctrl = nullptr;
        const K* m_keys = nullptr;
        isize m_idx = 0;
        isize m_cap = 0;
    };

    explicit HashSet(Allocator* alloc = nullptr) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    ~HashSet() noexcept {
        destroy_elements();
        free_tables();
    }

    HashSet(const HashSet&) = delete;
    HashSet& operator=(const HashSet&) = delete;

    HashSet(HashSet&& other) noexcept
        : m_ctrl(other.m_ctrl), m_keys(other.m_keys), m_size(other.m_size), m_cap(other.m_cap),
          m_tombstones(other.m_tombstones), m_alloc(other.m_alloc) {
        other.m_ctrl = nullptr;
        other.m_keys = nullptr;
        other.m_size = 0;
        other.m_cap = 0;
        other.m_tombstones = 0;
    }

    HashSet& operator=(HashSet&& other) noexcept {
        if (this != &other) {
            destroy_elements();
            free_tables();
            m_ctrl = other.m_ctrl;
            m_keys = other.m_keys;
            m_size = other.m_size;
            m_cap = other.m_cap;
            m_tombstones = other.m_tombstones;
            m_alloc = other.m_alloc;
            other.m_ctrl = nullptr;
            other.m_keys = nullptr;
            other.m_size = 0;
            other.m_cap = 0;
            other.m_tombstones = 0;
        }
        return *this;
    }

    [[nodiscard]] HashSet clone(Allocator* alloc = nullptr) const {
        HashSet copy(alloc ? alloc : m_alloc);
        copy.reserve(m_size);
        for (isize i = 0; i < m_cap; ++i) {
            if (is_occupied(m_ctrl[i])) {
                if constexpr (requires(const K& k) { k.clone(); }) {
                    copy.insert(m_keys[i].clone(copy.m_alloc));
                } else {
                    copy.insert(m_keys[i]);
                }
            }
        }
        return copy;
    }

    [[nodiscard]] isize size() const noexcept { return m_size; }
    [[nodiscard]] isize capacity() const noexcept { return m_cap; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] ConstIterator begin() const noexcept { return ConstIterator(m_ctrl, m_keys, 0, m_cap); }
    [[nodiscard]] ConstIterator end() const noexcept { return ConstIterator(m_ctrl, m_keys, m_cap, m_cap); }

    void clear() noexcept {
        destroy_elements();
        if (m_ctrl) {
            mem_zero(m_ctrl, static_cast<usize>(m_cap));
        }
        m_size = 0;
        m_tombstones = 0;
    }

    void reserve(isize min_items) {
        if (min_items <= 0) return;
        const isize needed_cap = static_cast<isize>(bit_ceil(static_cast<u64>((min_items * 8 + 6) / 7)));
        if (needed_cap > m_cap) {
            rehash(keel::max<isize>(needed_cap, 8));
        }
    }

    template <typename KeyLike> [[nodiscard]] bool contains(const KeyLike& key) const noexcept {
        return find_slot(key) >= 0;
    }

    // Inserts `key`. Returns true if newly inserted, false if already present.
    bool insert(K key) {
        ensure_for_insert();
        const u64 h = HashFn{}(key);
        const u8 tag = make_tag(h);
        const isize mask = m_cap - 1;
        isize idx = static_cast<isize>(h) & mask;
        isize first_tombstone = kInvalidIndex;
        for (;;) {
            const u8 c = m_ctrl[idx];
            if (c == kCtrlEmpty) {
                if (first_tombstone >= 0) idx = first_tombstone;
                break;
            }
            if (c == kCtrlDeleted) {
                if (first_tombstone < 0) first_tombstone = idx;
            } else if (c == tag && EqualFn{}(m_keys[idx], key)) {
                return false;
            }
            idx = (idx + 1) & mask;
        }
        if (m_ctrl[idx] == kCtrlDeleted) --m_tombstones;
        m_ctrl[idx] = tag;
        ::new (m_keys + idx) K(keel::move(key));
        ++m_size;
        return true;
    }

    template <typename KeyLike> bool remove(const KeyLike& key) noexcept {
        const isize idx = find_slot(key);
        if (idx < 0) return false;
        m_keys[idx].~K();
        m_ctrl[idx] = kCtrlDeleted;
        --m_size;
        ++m_tombstones;
        return true;
    }

private:
    template <typename KeyLike> [[nodiscard]] isize find_slot(const KeyLike& key) const noexcept {
        if (m_size == 0) return kInvalidIndex;
        const u64 h = HashFn{}(key);
        const u8 tag = make_tag(h);
        const isize mask = m_cap - 1;
        isize idx = static_cast<isize>(h) & mask;
        for (isize step = 0; step < m_cap; ++step) {
            const u8 c = m_ctrl[idx];
            if (c == kCtrlEmpty) return kInvalidIndex;
            if (c == tag && EqualFn{}(m_keys[idx], key)) return idx;
            idx = (idx + 1) & mask;
        }
        return kInvalidIndex;
    }

    void ensure_for_insert() {
        if (m_cap == 0) {
            rehash(8);
        } else if ((m_size + m_tombstones + 1) * 8 > m_cap * 7) {
            const isize next_cap = (m_size * 2 >= m_cap) ? (m_cap * 2) : m_cap;
            rehash(next_cap);
        }
    }

    void rehash(isize new_cap) {
        u8* new_ctrl = m_alloc->alloc_array<u8>(new_cap);
        mem_zero(new_ctrl, static_cast<usize>(new_cap));
        K* new_keys = m_alloc->alloc_array<K>(new_cap);

        const isize mask = new_cap - 1;
        for (isize i = 0; i < m_cap; ++i) {
            if (is_occupied(m_ctrl[i])) {
                const u64 h = HashFn{}(m_keys[i]);
                const u8 tag = make_tag(h);
                isize idx = static_cast<isize>(h) & mask;
                while (new_ctrl[idx] != kCtrlEmpty) {
                    idx = (idx + 1) & mask;
                }
                new_ctrl[idx] = tag;
                ::new (new_keys + idx) K(keel::move(m_keys[i]));
                m_keys[i].~K();
            }
        }

        free_tables();
        m_ctrl = new_ctrl;
        m_keys = new_keys;
        m_cap = new_cap;
        m_tombstones = 0;
    }

    void destroy_elements() noexcept {
        if (!m_ctrl) return;
        if constexpr (!IsTriviallyDestructible<K>) {
            for (isize i = 0; i < m_cap; ++i) {
                if (is_occupied(m_ctrl[i])) {
                    m_keys[i].~K();
                }
            }
        }
    }

    void free_tables() noexcept {
        if (m_ctrl) {
            m_alloc->free_array(m_ctrl, m_cap);
            m_alloc->free_array(m_keys, m_cap);
            m_ctrl = nullptr;
            m_keys = nullptr;
        }
    }

    u8* m_ctrl = nullptr;
    K* m_keys = nullptr;
    isize m_size = 0;
    isize m_cap = 0;
    isize m_tombstones = 0;
    Allocator* m_alloc = nullptr;
};

} // namespace keel
