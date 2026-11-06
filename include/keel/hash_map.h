#pragma once

#include "keel/bits.h"
#include "keel/memory.h"
#include "keel/ref.h"

namespace keel {

template <typename V> struct InsertResult {
    V* value = nullptr;
    bool inserted = false;
};

// Open-addressed hash map with flat control-byte metadata and separate flat key/value arrays.
// Pointers to keys/values may be invalidated on insert/rehash.
template <typename K, typename V, typename HashFn = Hash<K>, typename EqualFn = Equal<K>> class HashMap {
    static_assert(IsNothrowMoveConstructible<K> && IsNothrowDestructible<K>,
                  "HashMap key type K requires noexcept move and destructor");
    static_assert(IsNothrowMoveConstructible<V> && IsNothrowDestructible<V>,
                  "HashMap value type V requires noexcept move and destructor");

    static constexpr u8 kCtrlEmpty = 0x00;
    static constexpr u8 kCtrlDeleted = 0x01;

    [[nodiscard]] static constexpr u8 make_tag(u64 h) noexcept { return static_cast<u8>(0x80u | (h >> 57)); }
    [[nodiscard]] static constexpr bool is_occupied(u8 ctrl) noexcept { return (ctrl & 0x80u) != 0; }

public:
    struct Entry {
        const K& key;
        V& value;
    };

    struct ConstEntry {
        const K& key;
        const V& value;
    };

    class Iterator {
    public:
        constexpr Iterator(const u8* ctrl, const K* keys, V* vals, isize idx, isize cap) noexcept
            : m_ctrl(ctrl), m_keys(keys), m_vals(vals), m_idx(idx), m_cap(cap) {
            advance_to_occupied();
        }

        [[nodiscard]] constexpr Entry operator*() const noexcept { return Entry{m_keys[m_idx], m_vals[m_idx]}; }

        constexpr Iterator& operator++() noexcept {
            ++m_idx;
            advance_to_occupied();
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const Iterator& other) const noexcept { return m_idx == other.m_idx; }
        [[nodiscard]] constexpr bool operator!=(const Iterator& other) const noexcept { return m_idx != other.m_idx; }

    private:
        constexpr void advance_to_occupied() noexcept {
            while (m_idx < m_cap && !is_occupied(m_ctrl[m_idx])) {
                ++m_idx;
            }
        }

        const u8* m_ctrl = nullptr;
        const K* m_keys = nullptr;
        V* m_vals = nullptr;
        isize m_idx = 0;
        isize m_cap = 0;
    };

    class ConstIterator {
    public:
        constexpr ConstIterator(const u8* ctrl, const K* keys, const V* vals, isize idx, isize cap) noexcept
            : m_ctrl(ctrl), m_keys(keys), m_vals(vals), m_idx(idx), m_cap(cap) {
            advance_to_occupied();
        }

        [[nodiscard]] constexpr ConstEntry operator*() const noexcept {
            return ConstEntry{m_keys[m_idx], m_vals[m_idx]};
        }

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
        const V* m_vals = nullptr;
        isize m_idx = 0;
        isize m_cap = 0;
    };

    explicit HashMap(Allocator* alloc = nullptr) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    ~HashMap() noexcept {
        destroy_elements();
        free_tables();
    }

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    HashMap(HashMap&& other) noexcept
        : m_ctrl(other.m_ctrl), m_keys(other.m_keys), m_vals(other.m_vals), m_size(other.m_size), m_cap(other.m_cap),
          m_tombstones(other.m_tombstones), m_alloc(other.m_alloc) {
        other.m_ctrl = nullptr;
        other.m_keys = nullptr;
        other.m_vals = nullptr;
        other.m_size = 0;
        other.m_cap = 0;
        other.m_tombstones = 0;
    }

    HashMap& operator=(HashMap&& other) noexcept {
        if (this != &other) {
            destroy_elements();
            free_tables();
            m_ctrl = other.m_ctrl;
            m_keys = other.m_keys;
            m_vals = other.m_vals;
            m_size = other.m_size;
            m_cap = other.m_cap;
            m_tombstones = other.m_tombstones;
            m_alloc = other.m_alloc;
            other.m_ctrl = nullptr;
            other.m_keys = nullptr;
            other.m_vals = nullptr;
            other.m_size = 0;
            other.m_cap = 0;
            other.m_tombstones = 0;
        }
        return *this;
    }

    [[nodiscard]] HashMap clone(Allocator* alloc = nullptr) const {
        HashMap copy(alloc ? alloc : m_alloc);
        copy.reserve(m_size);
        for (isize i = 0; i < m_cap; ++i) {
            if (is_occupied(m_ctrl[i])) {
                if constexpr (requires(const K& k) { k.clone(); } && requires(const V& v) { v.clone(); }) {
                    copy.put(m_keys[i].clone(copy.m_alloc), m_vals[i].clone(copy.m_alloc));
                } else if constexpr (requires(const K& k) { k.clone(); }) {
                    copy.put(m_keys[i].clone(copy.m_alloc), m_vals[i]);
                } else if constexpr (requires(const V& v) { v.clone(); }) {
                    copy.put(m_keys[i], m_vals[i].clone(copy.m_alloc));
                } else {
                    copy.put(m_keys[i], m_vals[i]);
                }
            }
        }
        return copy;
    }

    [[nodiscard]] isize size() const noexcept { return m_size; }
    [[nodiscard]] isize capacity() const noexcept { return m_cap; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] Iterator begin() noexcept { return Iterator(m_ctrl, m_keys, m_vals, 0, m_cap); }
    [[nodiscard]] Iterator end() noexcept { return Iterator(m_ctrl, m_keys, m_vals, m_cap, m_cap); }
    [[nodiscard]] ConstIterator begin() const noexcept { return ConstIterator(m_ctrl, m_keys, m_vals, 0, m_cap); }
    [[nodiscard]] ConstIterator end() const noexcept { return ConstIterator(m_ctrl, m_keys, m_vals, m_cap, m_cap); }

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
            rehash(max<isize>(needed_cap, 8));
        }
    }

    template <typename KeyLike> [[nodiscard]] V* get(const KeyLike& key) noexcept {
        const isize idx = find_slot(key);
        return (idx >= 0) ? &m_vals[idx] : nullptr;
    }

    template <typename KeyLike> [[nodiscard]] const V* get(const KeyLike& key) const noexcept {
        const isize idx = find_slot(key);
        return (idx >= 0) ? &m_vals[idx] : nullptr;
    }

    template <typename KeyLike> [[nodiscard]] bool contains(const KeyLike& key) const noexcept {
        return find_slot(key) >= 0;
    }

    // Inserts or overwrites `key` with `val`. Returns true if newly inserted, false if overwritten.
    bool put(K key, V val) {
        ensure_for_insert();
        const u64 h = HashFn{}(key);
        const u8 tag = make_tag(h);
        const isize idx = find_or_prepare_insert(key, h, tag);
        if (is_occupied(m_ctrl[idx])) {
            m_vals[idx] = move(val);
            return false;
        }
        if (m_ctrl[idx] == kCtrlDeleted) --m_tombstones;
        m_ctrl[idx] = tag;
        ::new (m_keys + idx) K(move(key));
        ::new (m_vals + idx) V(move(val));
        ++m_size;
        return true;
    }

    // Inserts `key`/`val` only if `key` is not already present.
    InsertResult<V> try_insert(K key, V val) {
        ensure_for_insert();
        const u64 h = HashFn{}(key);
        const u8 tag = make_tag(h);
        const isize idx = find_or_prepare_insert(key, h, tag);
        if (is_occupied(m_ctrl[idx])) {
            return {&m_vals[idx], false};
        }
        if (m_ctrl[idx] == kCtrlDeleted) --m_tombstones;
        m_ctrl[idx] = tag;
        ::new (m_keys + idx) K(move(key));
        ::new (m_vals + idx) V(move(val));
        ++m_size;
        return {&m_vals[idx], true};
    }

    // Returns reference to existing value or inserts `default_val` and returns reference to it.
    V& get_or_insert(const K& key, V default_val = V{}) {
        ensure_for_insert();
        const u64 h = HashFn{}(key);
        const u8 tag = make_tag(h);
        const isize idx = find_or_prepare_insert(key, h, tag);
        if (!is_occupied(m_ctrl[idx])) {
            if (m_ctrl[idx] == kCtrlDeleted) --m_tombstones;
            m_ctrl[idx] = tag;
            if constexpr (requires(const K& k) { k.clone(); }) {
                ::new (m_keys + idx) K(key.clone(m_alloc));
            } else {
                ::new (m_keys + idx) K(key);
            }
            ::new (m_vals + idx) V(move(default_val));
            ++m_size;
        }
        return m_vals[idx];
    }

    template <typename KeyLike> bool remove(const KeyLike& key) noexcept {
        const isize idx = find_slot(key);
        if (idx < 0) return false;
        m_keys[idx].~K();
        m_vals[idx].~V();
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
            if (c == tag && EqualFn{}(m_keys[idx], key)) {
                return idx;
            }
            idx = (idx + 1) & mask;
        }
        return kInvalidIndex;
    }

    [[nodiscard]] isize find_or_prepare_insert(const K& key, u64 h, u8 tag) const noexcept {
        const isize mask = m_cap - 1;
        isize idx = static_cast<isize>(h) & mask;
        isize first_tombstone = kInvalidIndex;
        for (;;) {
            const u8 c = m_ctrl[idx];
            if (c == kCtrlEmpty) {
                return (first_tombstone >= 0) ? first_tombstone : idx;
            }
            if (c == kCtrlDeleted) {
                if (first_tombstone < 0) first_tombstone = idx;
            } else if (c == tag && EqualFn{}(m_keys[idx], key)) {
                return idx;
            }
            idx = (idx + 1) & mask;
        }
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
        V* new_vals = m_alloc->alloc_array<V>(new_cap);

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
                ::new (new_keys + idx) K(move(m_keys[i]));
                ::new (new_vals + idx) V(move(m_vals[i]));
                m_keys[i].~K();
                m_vals[i].~V();
            }
        }

        free_tables();
        m_ctrl = new_ctrl;
        m_keys = new_keys;
        m_vals = new_vals;
        m_cap = new_cap;
        m_tombstones = 0;
    }

    void destroy_elements() noexcept {
        if (!m_ctrl) return;
        if constexpr (!IsTriviallyDestructible<K> || !IsTriviallyDestructible<V>) {
            for (isize i = 0; i < m_cap; ++i) {
                if (is_occupied(m_ctrl[i])) {
                    m_keys[i].~K();
                    m_vals[i].~V();
                }
            }
        }
    }

    void free_tables() noexcept {
        if (m_ctrl) {
            m_alloc->free_array(m_ctrl, m_cap);
            m_alloc->free_array(m_keys, m_cap);
            m_alloc->free_array(m_vals, m_cap);
            m_ctrl = nullptr;
            m_keys = nullptr;
            m_vals = nullptr;
        }
    }

    u8* m_ctrl = nullptr;
    K* m_keys = nullptr;
    V* m_vals = nullptr;
    isize m_size = 0;
    isize m_cap = 0;
    isize m_tombstones = 0;
    Allocator* m_alloc = nullptr;
};

} // namespace keel
