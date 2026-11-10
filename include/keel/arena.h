#pragma once

#include "keel/memory.h"
#include "keel/ref.h"

namespace keel {

class Arena;

class ArenaTemp {
public:
    ArenaTemp() noexcept = default;
    ArenaTemp(Arena& arena, usize saved_offset) noexcept : m_arena(&arena), m_saved_offset(saved_offset) {}
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
    usize m_saved_offset = 0;
};

class Arena {
public:
    static constexpr usize kDefaultReserveSize = (sizeof(void*) >= 8) ? (64ULL * 1024 * 1024) : (4ULL * 1024 * 1024);

    explicit Arena(usize reserve_size = kDefaultReserveSize) noexcept;
    ~Arena() noexcept;

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;

    [[nodiscard]] void* alloc(usize size, usize align = kDefaultAlign);
    [[nodiscard]] void* realloc(void* ptr, usize old_size, usize new_size, usize align = kDefaultAlign);
    [[nodiscard]] bool try_extend(void* ptr, usize old_size, usize new_size) noexcept;

    template <typename T> [[nodiscard]] T* alloc_array(usize count) {
        if (count == 0) return nullptr;
        if (count > (~usize{0}) / sizeof(T)) [[unlikely]] {
            handle_oom(~usize{0}, alignof(T));
        }
        return static_cast<T*>(alloc(count * sizeof(T), alignof(T)));
    }

    template <typename T, typename... Args> [[nodiscard]] T* create(Args&&... args) {
        if constexpr (IsTriviallyDestructible<T>) {
            void* mem = alloc(sizeof(T), alignof(T));
            return ::new (mem) T(keel::forward<Args>(args)...);
        } else {
            const usize mark = m_offset;
            void* mem = alloc(sizeof(T), alignof(T));
            auto* node = static_cast<DtorNode*>(alloc(sizeof(DtorNode), alignof(DtorNode)));
            T* obj = ::new (mem) T(keel::forward<Args>(args)...);
            node->obj = obj;
            node->destroy = [](void* p) noexcept { static_cast<T*>(p)->~T(); };
            node->next = m_dtor_head;
            node->offset = mark;
            m_dtor_head = node;
            return obj;
        }
    }

    template <typename T> [[nodiscard]] T* create_array(usize count) {
        if (count == 0) return nullptr;
        if constexpr (IsTriviallyDestructible<T>) {
            T* arr = alloc_array<T>(count);
            for (usize i = 0; i < count; ++i) {
                ::new (arr + i) T{};
            }
            return arr;
        } else {
            struct ArrayDtorRecord {
                DtorNode node{};
                T* data = nullptr;
                usize count = 0;
            };
            const usize mark = m_offset;
            T* arr = alloc_array<T>(count);
            auto* rec = static_cast<ArrayDtorRecord*>(alloc(sizeof(ArrayDtorRecord), alignof(ArrayDtorRecord)));
            for (usize i = 0; i < count; ++i) {
                ::new (arr + i) T{};
            }
            rec->data = arr;
            rec->count = count;
            rec->node.obj = rec;
            rec->node.destroy = [](void* p) noexcept {
                auto* r = static_cast<ArrayDtorRecord*>(p);
                for (usize i = r->count; i > 0; --i) {
                    r->data[i - 1].~T();
                }
            };
            rec->node.next = m_dtor_head;
            rec->node.offset = mark;
            m_dtor_head = &rec->node;
            return arr;
        }
    }

    template <typename T> [[nodiscard]] MutArrayRef<T> create_slice(usize count) {
        return MutArrayRef<T>(create_array<T>(count), count);
    }

    [[nodiscard]] StringRef alloc_string(StringRef str) {
        if (str.empty()) return StringRef("", 0);
        char* buf = static_cast<char*>(alloc(str.size() + 1, 1));
        mem_copy(buf, str.data(), str.size());
        buf[str.size()] = '\0';
        return StringRef(buf, str.size());
    }

    void reset() noexcept;
    void rewind_to(usize offset) noexcept;
    void release_memory() noexcept;

    [[nodiscard]] usize used() const noexcept { return m_offset; }
    [[nodiscard]] usize committed() const noexcept { return m_committed; }
    [[nodiscard]] usize reserved() const noexcept { return m_reserved; }

    [[nodiscard]] ArenaTemp begin_temp() noexcept { return ArenaTemp(*this, m_offset); }
    [[nodiscard]] Allocator* allocator() noexcept { return &m_allocator; }

private:
    struct DtorNode {
        void* obj = nullptr;
        void (*destroy)(void*) noexcept = nullptr;
        DtorNode* next = nullptr;
        usize offset = 0;
    };

    void init_allocator() noexcept;
    void run_destructors_from(usize offset) noexcept;

    u8* m_base = nullptr;
    usize m_offset = 0;
    usize m_committed = 0;
    usize m_reserved = 0;
    usize m_last_alloc_end = 0;
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
[[nodiscard]] ArenaTemp get_scratch(Arena* const* conflicts = nullptr, usize conflict_count = 0) noexcept;
[[nodiscard]] ArenaTemp get_scratch(const Allocator* conflict_alloc) noexcept;

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

// Arena-backed move-only contiguous dynamic array.
// Trivially destructible when `T` is trivially destructible, and expands in-place via `Arena::try_extend`
// when growing at the tip of the arena.
template <typename T> class ArenaArray : private detail::ArenaArrayBase<T> {
    static_assert(IsNothrowMoveConstructible<T>, "ArenaArray<T> requires noexcept move constructor");
    static_assert(IsNothrowDestructible<T>, "ArenaArray<T> requires noexcept destructor");

    using Base = detail::ArenaArrayBase<T>;
    using Base::m_arena;
    using Base::m_cap;
    using Base::m_data;
    using Base::m_size;

public:
    constexpr ArenaArray() noexcept = default;

    explicit ArenaArray(Arena& arena, usize initial_reserve = 0) : Base(&arena) {
        if (initial_reserve > 0) {
            reserve(initial_reserve);
        }
    }

    explicit ArenaArray(Arena* arena, usize initial_reserve = 0) : Base(arena) {
        if (arena && initial_reserve > 0) {
            reserve(initial_reserve);
        }
    }

    [[nodiscard]] static ArenaArray from(Arena& arena, ArrayRef<T> items) {
        ArenaArray arr(arena, items.size());
        arr.append(items);
        return arr;
    }

    ArenaArray(const ArenaArray&) = delete;
    ArenaArray& operator=(const ArenaArray&) = delete;

    constexpr ArenaArray(ArenaArray&& other) noexcept : Base(other.m_arena) {
        m_data = other.m_data;
        m_size = other.m_size;
        m_cap = other.m_cap;
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_cap = 0;
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

    [[nodiscard]] ArenaArray clone(Arena* dst_arena = nullptr) const {
        Arena* target = dst_arena ? dst_arena : m_arena;
        ArenaArray copy(target);
        if (m_size > 0) {
            KEEL_ASSERT(target != nullptr);
            copy.reserve(m_size);
            if constexpr (IsTriviallyCopyable<T>) {
                mem_copy(copy.m_data, m_data, sizeof(T) * m_size);
                copy.m_size = m_size;
            } else {
                Allocator* dst_alloc = target->allocator();
                for (u32 i = 0; i < m_size; ++i) {
                    copy.push_back(detail::clone_value(m_data[i], dst_alloc));
                }
            }
        }
        return copy;
    }

    constexpr void bind_arena(Arena& arena) noexcept { m_arena = &arena; }
    [[nodiscard]] constexpr Arena* arena() const noexcept { return m_arena; }

    [[nodiscard]] constexpr T* data() noexcept { return m_data; }
    [[nodiscard]] constexpr const T* data() const noexcept { return m_data; }
    [[nodiscard]] constexpr usize size() const noexcept { return static_cast<usize>(m_size); }
    [[nodiscard]] constexpr usize capacity() const noexcept { return static_cast<usize>(m_cap); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] constexpr T* begin() noexcept { return m_data; }
    [[nodiscard]] constexpr const T* begin() const noexcept { return m_data; }
    [[nodiscard]] constexpr T* end() noexcept { return m_data + m_size; }
    [[nodiscard]] constexpr const T* end() const noexcept { return m_data + m_size; }

    [[nodiscard]] constexpr T& operator[](usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < static_cast<usize>(m_size));
        return m_data[i];
    }

    [[nodiscard]] constexpr const T& operator[](usize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i < static_cast<usize>(m_size));
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
    [[nodiscard]] constexpr ArrayRef<T> as_ref() const noexcept { return ArrayRef<T>(m_data, m_size); }
    [[nodiscard]] constexpr MutArrayRef<T> as_mut_ref() noexcept { return MutArrayRef<T>(m_data, m_size); }
    [[nodiscard]] constexpr ArrayRef<T> slice(usize offset, usize count = kInvalidIndex) const noexcept {
        return as_ref().slice(offset, count);
    }

    void clear() noexcept {
        if constexpr (!IsTriviallyDestructible<T>) {
            for (u32 i = m_size; i > 0; --i) {
                m_data[i - 1].~T();
            }
        }
        m_size = 0;
    }

    void reserve(usize new_cap) {
        if (new_cap <= static_cast<usize>(m_cap)) return;
        KEEL_ASSERT(m_arena != nullptr);
        KEEL_ASSERT(new_cap <= 0xFFFFFFFFu);
        if (!m_arena) return;
        if (new_cap > 0xFFFFFFFFu || new_cap > (~usize{0}) / sizeof(T)) [[unlikely]] {
            handle_oom(~usize{0}, alignof(T));
        }

        const usize old_bytes = sizeof(T) * static_cast<usize>(m_cap);
        const usize new_bytes = sizeof(T) * new_cap;
        if (m_data && m_arena->try_extend(m_data, old_bytes, new_bytes)) {
            m_cap = static_cast<u32>(new_cap);
            return;
        }

        T* new_buf = static_cast<T*>(m_arena->alloc(new_bytes, alignof(T)));
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

    void resize(usize new_size) {
        KEEL_ASSERT(new_size <= 0xFFFFFFFFu);
        if (new_size > 0xFFFFFFFFu) [[unlikely]] {
            handle_oom(~usize{0}, alignof(T));
        }
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

    void append(ArrayRef<T> items) {
        const usize count = items.size();
        if (count == 0) return;
        if (m_data && items.data() >= m_data && items.data() < m_data + m_size) {
            const usize offset = static_cast<usize>(items.data() - m_data);
            reserve(static_cast<usize>(m_size) + count);
            if constexpr (IsTriviallyCopyable<T>) {
                mem_copy(m_data + m_size, m_data + offset, count * sizeof(T));
            } else {
                for (usize i = 0; i < count; ++i) {
                    ::new (m_data + m_size + i) T(m_data[offset + i]);
                }
            }
            m_size += static_cast<u32>(count);
            return;
        }
        reserve(static_cast<usize>(m_size) + count);
        if constexpr (IsTriviallyCopyable<T>) {
            mem_copy(m_data + m_size, items.data(), count * sizeof(T));
        } else {
            for (usize i = 0; i < count; ++i) {
                ::new (m_data + m_size + i) T(items[i]);
            }
        }
        m_size += static_cast<u32>(count);
    }

    void assign(ArrayRef<T> items) {
        if (m_data && items.data() >= m_data && items.data() < m_data + m_size) {
            const usize offset = static_cast<usize>(items.data() - m_data);
            const usize count = items.size();
            if constexpr (IsTriviallyCopyable<T>) {
                if (count > 0 && offset > 0) {
                    mem_move(m_data, m_data + offset, count * sizeof(T));
                }
            } else {
                for (usize i = 0; i < count; ++i) {
                    if (offset > 0) {
                        m_data[i] = keel::move(m_data[offset + i]);
                    }
                }
                for (usize i = count; i < m_size; ++i) {
                    m_data[i].~T();
                }
            }
            m_size = static_cast<u32>(count);
            return;
        }
        clear();
        append(items);
    }

    void pop_back() noexcept {
        KEEL_BOUNDS_ASSERT(m_size > 0);
        --m_size;
        if constexpr (!IsTriviallyDestructible<T>) {
            m_data[m_size].~T();
        }
    }

    void remove_at(usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < static_cast<usize>(m_size));
        const usize tail = static_cast<usize>(m_size) - i - 1;
        if constexpr (IsTriviallyCopyable<T>) {
            if (tail > 0) {
                mem_move(m_data + i, m_data + i + 1, tail * sizeof(T));
            }
        } else {
            for (usize k = i; k < static_cast<usize>(m_size) - 1; ++k) {
                m_data[k] = keel::move(m_data[k + 1]);
            }
            m_data[m_size - 1].~T();
        }
        --m_size;
    }

    void erase(usize i) noexcept { remove_at(i); }

    void swap_remove(usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < static_cast<usize>(m_size));
        --m_size;
        if (i != static_cast<usize>(m_size)) {
            m_data[i] = keel::move(m_data[m_size]);
        }
        if constexpr (!IsTriviallyDestructible<T>) {
            m_data[m_size].~T();
        }
    }

    [[nodiscard]] friend constexpr bool operator==(const ArenaArray& a, const ArenaArray& b) noexcept
        requires requires(const T& x) {
            { x == x } -> detail::ConvertibleTo<bool>;
        }
    {
        return a.as_ref() == b.as_ref();
    }

    [[nodiscard]] friend constexpr bool operator==(const ArenaArray& a, ArrayRef<T> b) noexcept
        requires requires(const T& x) {
            { x == x } -> detail::ConvertibleTo<bool>;
        }
    {
        return a.as_ref() == b;
    }

private:
    template <typename... Args> T& emplace_back_slow(Args&&... args) {
        KEEL_ASSERT(m_arena != nullptr);
        if (m_cap > 0x7FFFFFFFu) [[unlikely]] {
            handle_oom(~usize{0}, alignof(T));
        }
        const u32 next_cap = (m_cap == 0) ? 16u : (m_cap * 2u);
        const usize old_bytes = sizeof(T) * static_cast<usize>(m_cap);
        const usize new_bytes = sizeof(T) * static_cast<usize>(next_cap);
        if (m_data && m_arena->try_extend(m_data, old_bytes, new_bytes)) {
            m_cap = next_cap;
            T* elem = ::new (m_data + m_size) T(keel::forward<Args>(args)...);
            ++m_size;
            return *elem;
        }

        T* new_buf = static_cast<T*>(m_arena->alloc(new_bytes, alignof(T)));
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

static_assert(IsTriviallyDestructible<ArenaArray<int>>, "ArenaArray<T> must remain trivially destructible for POD T");

} // namespace keel
