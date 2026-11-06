#pragma once

#include "keel/memory.h"

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
        void* mem = alloc(static_cast<isize>(sizeof(T)), static_cast<isize>(alignof(T)));
        return ::new (mem) T(forward<Args>(args)...);
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
    void init_allocator() noexcept;

    u8* m_base = nullptr;
    isize m_offset = 0;
    isize m_committed = 0;
    isize m_reserved = 0;
    isize m_last_alloc_end = 0;
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

} // namespace keel
