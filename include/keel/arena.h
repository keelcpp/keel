#pragma once

#include "keel/memory.h"
#include "keel/ref.h"

namespace keel {

class Arena;

class ArenaTemp {
public:
    ArenaTemp() noexcept = default;
    ArenaTemp(Arena& arena, isize saved_offset) noexcept : m_arena(&arena), m_saved_offset(saved_offset) {}
    ~ArenaTemp() noexcept;

    ArenaTemp(const ArenaTemp&) = delete;
    ArenaTemp& operator=(const ArenaTemp&) = delete;

    ArenaTemp(ArenaTemp&& other) noexcept : m_arena(other.m_arena), m_saved_offset(other.m_saved_offset) {
        other.m_arena = nullptr;
        other.m_saved_offset = 0;
    }

    ArenaTemp& operator=(ArenaTemp&& other) noexcept {
        if (this != &other) {
            release();
            m_arena = other.m_arena;
            m_saved_offset = other.m_saved_offset;
            other.m_arena = nullptr;
            other.m_saved_offset = 0;
        }
        return *this;
    }

    [[nodiscard]] Arena& arena() const noexcept {
        KEEL_ASSERT(m_arena != nullptr);
        return *m_arena;
    }

    [[nodiscard]] Allocator* allocator() const noexcept;
    void release() noexcept;

private:
    Arena* m_arena = nullptr;
    isize m_saved_offset = 0;
};

class Arena {
public:
    static constexpr isize kDefaultReserveSize = (sizeof(void*) >= 8) ? (64LL * 1024 * 1024) : (4 * 1024 * 1024);

    explicit Arena(isize reserve_size = kDefaultReserveSize) noexcept;
    ~Arena() noexcept;

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;

    [[nodiscard]] void* alloc(isize size, isize align = kDefaultAlign);
    [[nodiscard]] void* realloc(void* ptr, isize old_size, isize new_size, isize align = kDefaultAlign);
    [[nodiscard]] bool try_extend(void* ptr, isize old_size, isize new_size) noexcept;

    template <typename T> [[nodiscard]] T* alloc_array(isize count) {
        KEEL_ASSERT(count >= 0);
        if (count == 0) return nullptr;
        return static_cast<T*>(alloc(count * static_cast<isize>(sizeof(T)), static_cast<isize>(alignof(T))));
    }

    template <typename T, typename... Args> [[nodiscard]] T* create(Args&&... args) {
        if constexpr (IsTriviallyDestructible<T>) {
            void* mem = alloc(static_cast<isize>(sizeof(T)), static_cast<isize>(alignof(T)));
            return ::new (mem) T(keel::forward<Args>(args)...);
        } else {
            const isize mark = m_offset;
            void* mem = alloc(static_cast<isize>(sizeof(T)), static_cast<isize>(alignof(T)));
            auto* node = static_cast<DtorNode*>(alloc(static_cast<isize>(sizeof(DtorNode)), static_cast<isize>(alignof(DtorNode))));
            T* obj = ::new (mem) T(keel::forward<Args>(args)...);
            node->obj = obj;
            node->destroy = [](void* p) noexcept { static_cast<T*>(p)->~T(); };
            node->next = m_dtor_head;
            node->offset = mark;
            m_dtor_head = node;
            return obj;
        }
    }

    template <typename T> [[nodiscard]] T* create_array(isize count) {
        KEEL_ASSERT(count >= 0);
        if (count == 0) return nullptr;
        if constexpr (IsTriviallyDestructible<T>) {
            T* arr = alloc_array<T>(count);
            for (isize i = 0; i < count; ++i) {
                ::new (arr + i) T{};
            }
            return arr;
        } else {
            struct ArrayDtorRecord {
                DtorNode node{};
                T* data = nullptr;
                isize count = 0;
            };
            const isize mark = m_offset;
            T* arr = alloc_array<T>(count);
            auto* rec = static_cast<ArrayDtorRecord*>(
                alloc(static_cast<isize>(sizeof(ArrayDtorRecord)), static_cast<isize>(alignof(ArrayDtorRecord))));
            for (isize i = 0; i < count; ++i) {
                ::new (arr + i) T{};
            }
            rec->data = arr;
            rec->count = count;
            rec->node.obj = rec;
            rec->node.destroy = [](void* p) noexcept {
                auto* r = static_cast<ArrayDtorRecord*>(p);
                for (isize i = r->count; i > 0; --i) {
                    r->data[i - 1].~T();
                }
            };
            rec->node.next = m_dtor_head;
            rec->node.offset = mark;
            m_dtor_head = &rec->node;
            return arr;
        }
    }

    template <typename T> [[nodiscard]] MutArrayRef<T> create_slice(isize count) {
        return MutArrayRef<T>(create_array<T>(count), count);
    }

    [[nodiscard]] StringRef alloc_string(StringRef str) {
        if (str.empty()) return StringRef("", 0);
        char* buf = static_cast<char*>(alloc(str.size() + 1, 1));
        mem_copy(buf, str.data(), static_cast<usize>(str.size()));
        buf[str.size()] = '\0';
        return StringRef(buf, str.size());
    }

    void reset() noexcept;
    void rewind_to(isize offset) noexcept;
    void release_memory() noexcept;

    [[nodiscard]] isize used() const noexcept { return m_offset; }
    [[nodiscard]] isize committed() const noexcept { return m_committed; }
    [[nodiscard]] isize reserved() const noexcept { return m_reserved; }

    [[nodiscard]] ArenaTemp begin_temp() noexcept { return ArenaTemp(*this, m_offset); }
    [[nodiscard]] Allocator* allocator() noexcept { return &m_allocator; }

private:
    struct DtorNode {
        void* obj = nullptr;
        void (*destroy)(void*) noexcept = nullptr;
        DtorNode* next = nullptr;
        isize offset = 0;
    };

    void init_allocator() noexcept;
    void run_destructors_from(isize offset) noexcept;

    u8* m_base = nullptr;
    isize m_offset = 0;
    isize m_committed = 0;
    isize m_reserved = 0;
    isize m_last_alloc_end = 0;
    DtorNode* m_dtor_head = nullptr;
    Allocator m_allocator{};
};

inline ArenaTemp::~ArenaTemp() noexcept { release(); }

inline void ArenaTemp::release() noexcept {
    if (m_arena) {
        m_arena->rewind_to(m_saved_offset);
        m_arena = nullptr;
    }
}

inline Allocator* ArenaTemp::allocator() const noexcept {
    KEEL_ASSERT(m_arena != nullptr);
    return m_arena->allocator();
}

// Thread-local scratch arena acquisition with conflict avoidance (ping-pong between 2 thread-local arenas)
[[nodiscard]] ArenaTemp get_scratch(Arena* const* conflicts = nullptr, isize conflict_count = 0) noexcept;

namespace detail {

template <typename T, bool TriviallyDestructible = IsTriviallyDestructible<T>> struct ArenaArrayBase {
    Arena* m_arena = nullptr;
    T* m_data = nullptr;
    u32 m_size = 0;
    u32 m_cap = 0;

    constexpr ArenaArrayBase() noexcept = default;
    constexpr explicit ArenaArrayBase(Arena* arena) noexcept : m_arena(arena) {}
    ~ArenaArrayBase() noexcept = default;
};

template <typename T> struct ArenaArrayBase<T, false> {
    Arena* m_arena = nullptr;
    T* m_data = nullptr;
    u32 m_size = 0;
    u32 m_cap = 0;

    constexpr ArenaArrayBase() noexcept = default;
    constexpr explicit ArenaArrayBase(Arena* arena) noexcept : m_arena(arena) {}

    ~ArenaArrayBase() noexcept {
        for (u32 i = m_size; i > 0; --i) {
            m_data[i - 1].~T();
        }
    }
};

} // namespace detail

// Arena-backed contiguous dynamic array.
// Trivially destructible when `T` is trivially destructible, and expands in-place via `Arena::try_extend`
// when growing at the tip of the arena.
template <typename T> class ArenaArray : private detail::ArenaArrayBase<T> {
    using Base = detail::ArenaArrayBase<T>;
    using Base::m_arena;
    using Base::m_cap;
    using Base::m_data;
    using Base::m_size;

public:
    constexpr ArenaArray() noexcept = default;

    explicit ArenaArray(Arena& arena, isize initial_reserve = 0) : Base(&arena) {
        if (initial_reserve > 0) {
            reserve(initial_reserve);
        }
    }

    explicit ArenaArray(Arena* arena, isize initial_reserve = 0) : Base(arena) {
        if (arena && initial_reserve > 0) {
            reserve(initial_reserve);
        }
    }

    ArenaArray(const ArenaArray& other) : Base(other.m_arena) {
        if (other.m_size > 0 && m_arena) {
            reserve(static_cast<isize>(other.m_size));
            if constexpr (IsTriviallyCopyable<T>) {
                mem_copy(m_data, other.m_data, sizeof(T) * other.m_size);
            } else {
                for (u32 i = 0; i < other.m_size; ++i) {
                    ::new (m_data + i) T(other.m_data[i]);
                }
            }
            m_size = other.m_size;
        }
    }

    ArenaArray(ArenaArray&& other) noexcept : Base(other.m_arena) {
        m_data = other.m_data;
        m_size = other.m_size;
        m_cap = other.m_cap;
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_cap = 0;
    }

    ArenaArray& operator=(const ArenaArray& other) {
        if (this != &other) {
            clear();
            if (!m_arena) {
                m_arena = other.m_arena;
            }
            if (other.m_size > 0 && m_arena) {
                reserve(static_cast<isize>(other.m_size));
                if constexpr (IsTriviallyCopyable<T>) {
                    mem_copy(m_data, other.m_data, sizeof(T) * other.m_size);
                } else {
                    for (u32 i = 0; i < other.m_size; ++i) {
                        ::new (m_data + i) T(other.m_data[i]);
                    }
                }
                m_size = other.m_size;
            }
        }
        return *this;
    }

    ArenaArray& operator=(ArenaArray&& other) noexcept {
        if (this != &other) {
            clear();
            m_arena = other.m_arena;
            m_data = other.m_data;
            m_size = other.m_size;
            m_cap = other.m_cap;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_cap = 0;
        }
        return *this;
    }

    ~ArenaArray() noexcept = default;

    constexpr void bind_arena(Arena& arena) noexcept { m_arena = &arena; }
    [[nodiscard]] constexpr Arena* arena() const noexcept { return m_arena; }

    [[nodiscard]] constexpr T* data() noexcept { return m_data; }
    [[nodiscard]] constexpr const T* data() const noexcept { return m_data; }
    [[nodiscard]] constexpr isize size() const noexcept { return static_cast<isize>(m_size); }
    [[nodiscard]] constexpr isize capacity() const noexcept { return static_cast<isize>(m_cap); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] constexpr T* begin() noexcept { return m_data; }
    [[nodiscard]] constexpr const T* begin() const noexcept { return m_data; }
    [[nodiscard]] constexpr T* end() noexcept { return m_data + m_size; }
    [[nodiscard]] constexpr const T* end() const noexcept { return m_data + m_size; }

    [[nodiscard]] constexpr T& operator[](isize i) noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < static_cast<isize>(m_size));
        return m_data[i];
    }

    [[nodiscard]] constexpr const T& operator[](isize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i >= 0 && i < static_cast<isize>(m_size));
        return m_data[i];
    }

    [[nodiscard]] constexpr T& front() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] constexpr const T& front() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] constexpr T& back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    [[nodiscard]] constexpr const T& back() const noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    [[nodiscard]] constexpr operator ArrayRef<T>() const noexcept { return ArrayRef<T>(m_data, m_size); }
    [[nodiscard]] constexpr operator MutArrayRef<T>() noexcept { return MutArrayRef<T>(m_data, m_size); }

    void clear() noexcept {
        if constexpr (!IsTriviallyDestructible<T>) {
            for (u32 i = m_size; i > 0; --i) {
                m_data[i - 1].~T();
            }
        }
        m_size = 0;
    }

    void reserve(isize new_cap) {
        if (new_cap <= static_cast<isize>(m_cap)) return;
        KEEL_ASSERT(m_arena != nullptr);
        if (!m_arena) return;

        const isize old_bytes = static_cast<isize>(sizeof(T)) * static_cast<isize>(m_cap);
        const isize new_bytes = static_cast<isize>(sizeof(T)) * new_cap;
        if (m_data && m_arena->try_extend(m_data, old_bytes, new_bytes)) {
            m_cap = static_cast<u32>(new_cap);
            return;
        }

        T* new_buf = static_cast<T*>(m_arena->alloc(new_bytes, static_cast<isize>(alignof(T))));
        if (m_size > 0 && m_data) {
            if constexpr (IsTriviallyCopyable<T>) {
                mem_copy(new_buf, m_data, sizeof(T) * m_size);
            } else {
                for (u32 i = 0; i < m_size; ++i) {
                    ::new (new_buf + i) T(keel::move(m_data[i]));
                    if constexpr (!IsTriviallyDestructible<T>) m_data[i].~T();
                }
            }
        }
        m_data = new_buf;
        m_cap = static_cast<u32>(new_cap);
    }

    void resize(isize new_size) {
        KEEL_ASSERT(new_size >= 0);
        const u32 target = static_cast<u32>(new_size);
        if (target < m_size) {
            if constexpr (!IsTriviallyDestructible<T>) {
                for (u32 i = m_size; i > target; --i) {
                    m_data[i - 1].~T();
                }
            }
            m_size = target;
        } else if (target > m_size) {
            reserve(new_size);
            for (u32 i = m_size; i < target; ++i) {
                ::new (m_data + i) T{};
            }
            m_size = target;
        }
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
        if (m_size < m_cap) [[likely]] {
            T* elem = ::new (m_data + m_size) T(keel::forward<Args>(args)...);
            ++m_size;
            return *elem;
        }
        return emplace_back_slow(keel::forward<Args>(args)...);
    }

    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(keel::move(value)); }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        if constexpr (!IsTriviallyDestructible<T>) {
            m_data[m_size].~T();
        }
    }

private:
    template <typename... Args> T& emplace_back_slow(Args&&... args) {
        KEEL_ASSERT(m_arena != nullptr);
        const u32 next_cap = (m_cap == 0) ? 16u : (m_cap * 2u);
        const isize old_bytes = static_cast<isize>(sizeof(T)) * static_cast<isize>(m_cap);
        const isize new_bytes = static_cast<isize>(sizeof(T)) * static_cast<isize>(next_cap);
        if (m_data && m_arena->try_extend(m_data, old_bytes, new_bytes)) {
            m_cap = next_cap;
            T* elem = ::new (m_data + m_size) T(keel::forward<Args>(args)...);
            ++m_size;
            return *elem;
        }

        T* new_buf = static_cast<T*>(m_arena->alloc(new_bytes, static_cast<isize>(alignof(T))));
        // Construct new element before relocating/destroying old elements so self-aliasing args remain valid
        T* elem = ::new (new_buf + m_size) T(keel::forward<Args>(args)...);
        if (m_size > 0 && m_data) {
            if constexpr (IsTriviallyCopyable<T>) {
                mem_copy(new_buf, m_data, sizeof(T) * m_size);
            } else {
                for (u32 i = 0; i < m_size; ++i) {
                    ::new (new_buf + i) T(keel::move(m_data[i]));
                    if constexpr (!IsTriviallyDestructible<T>) m_data[i].~T();
                }
            }
        }
        m_data = new_buf;
        m_cap = next_cap;
        ++m_size;
        return *elem;
    }
};

} // namespace keel
