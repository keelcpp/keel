#include "keel/memory.h"
#include "keel/arena.h"
#include "keel/io.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace keel {

void mem_copy(void* dst, const void* src, usize bytes) noexcept {
    if (bytes > 0) {
        std::memcpy(dst, src, bytes);
    }
}

void mem_move(void* dst, const void* src, usize bytes) noexcept {
    if (bytes > 0) {
        std::memmove(dst, src, bytes);
    }
}

void mem_set(void* dst, u8 val, usize bytes) noexcept {
    if (bytes > 0) {
        std::memset(dst, val, bytes);
    }
}

int mem_cmp(const void* a, const void* b, usize bytes) noexcept {
    if (bytes == 0) return 0;
    return std::memcmp(a, b, bytes);
}

namespace {

constinit std::atomic<OomHandler> g_oom_handler{nullptr};
constinit std::atomic<void*> g_oom_user_data{nullptr};

void* sys_alloc(void*, usize size, usize align) noexcept {
    if (size == 0) return nullptr;
    const usize u_align = (align < kDefaultAlign) ? kDefaultAlign : align;
#if defined(_WIN32)
    return _aligned_malloc(size, u_align);
#else
    if (u_align <= alignof(std::max_align_t)) {
        return std::malloc(size);
    }
    void* ptr = nullptr;
    if (posix_memalign(&ptr, u_align, size) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

void* sys_realloc(void* ctx, void* ptr, usize old_size, usize new_size, usize align) noexcept {
    if (!ptr) return sys_alloc(ctx, new_size, align);
    if (new_size == 0) {
#if defined(_WIN32)
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
        return nullptr;
    }
    const usize u_align = (align < kDefaultAlign) ? kDefaultAlign : align;
#if defined(_WIN32)
    (void)old_size;
    return _aligned_realloc(ptr, new_size, u_align);
#else
    if (u_align <= alignof(std::max_align_t)) {
        return std::realloc(ptr, new_size);
    }
    void* new_ptr = sys_alloc(ctx, new_size, align);
    if (new_ptr && old_size > 0) {
        const usize copy_bytes = min(old_size, new_size);
        std::memcpy(new_ptr, ptr, copy_bytes);
        std::free(ptr);
    }
    return new_ptr;
#endif
}

void sys_free(void*, void* ptr, usize, usize) noexcept {
    if (!ptr) return;
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

constinit Allocator g_system_allocator{
    .alloc = &sys_alloc,
    .realloc = &sys_realloc,
    .free = &sys_free,
    .ctx = nullptr,
};

constinit std::atomic<Allocator*> g_default_allocator{&g_system_allocator};

constexpr usize kCommitGranularity = 64 * 1024; // Commit in 64 KB chunks

usize align_up(usize val, usize align) noexcept { return (val + align - 1) & ~(align - 1); }

} // namespace

void set_oom_handler(OomHandler handler, void* user_data) noexcept {
    g_oom_user_data.store(user_data, std::memory_order_release);
    g_oom_handler.store(handler, std::memory_order_release);
}

[[noreturn]] void handle_oom(usize size, usize align) {
    OomHandler handler = g_oom_handler.load(std::memory_order_acquire);
    if (handler) {
        handler(size, align, g_oom_user_data.load(std::memory_order_acquire));
    }
    stderr_write("keel: out of memory\n");
    stderr_flush();
    std::abort();
}

Allocator* default_allocator() noexcept { return g_default_allocator.load(std::memory_order_acquire); }

void set_default_allocator(Allocator* alloc) noexcept {
    g_default_allocator.store(alloc ? alloc : &g_system_allocator, std::memory_order_release);
}

// ============================================================================
// Arena
// ============================================================================

Arena::Arena(usize reserve_size) noexcept {
    init_allocator();
    if (reserve_size == 0) reserve_size = kDefaultReserveSize;
    m_reserved = align_up(reserve_size, kCommitGranularity);
#if defined(_WIN32)
    m_base = static_cast<u8*>(VirtualAlloc(nullptr, static_cast<SIZE_T>(m_reserved), MEM_RESERVE, PAGE_NOACCESS));
    if (!m_base) {
        m_reserved = 0;
    }
#elif defined(__EMSCRIPTEN__)
    // On 32-bit WebAssembly, virtual memory reservation without commit is not available;
    // allocate on demand starting with a modest initial buffer or reserve_size if small.
    m_committed = min<usize>(m_reserved, 256 * 1024);
    m_base = static_cast<u8*>(std::malloc(m_committed));
    if (!m_base) {
        m_committed = 0;
        m_reserved = 0;
    } else {
        m_reserved = m_committed;
    }
#else
    void* p = mmap(nullptr, m_reserved, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        m_base = nullptr;
        m_reserved = 0;
    } else {
        m_base = static_cast<u8*>(p);
    }
#endif
}

Arena::~Arena() noexcept { release_memory(); }

Arena::Arena(Arena&& other) noexcept
    : m_base(other.m_base), m_offset(other.m_offset), m_committed(other.m_committed), m_reserved(other.m_reserved),
      m_last_alloc_end(other.m_last_alloc_end), m_dtor_head(other.m_dtor_head) {
    init_allocator();
    other.m_base = nullptr;
    other.m_offset = 0;
    other.m_committed = 0;
    other.m_reserved = 0;
    other.m_last_alloc_end = 0;
    other.m_dtor_head = nullptr;
}

Arena& Arena::operator=(Arena&& other) noexcept {
    if (this != &other) {
        release_memory();
        m_base = other.m_base;
        m_offset = other.m_offset;
        m_committed = other.m_committed;
        m_reserved = other.m_reserved;
        m_last_alloc_end = other.m_last_alloc_end;
        m_dtor_head = other.m_dtor_head;
        other.m_base = nullptr;
        other.m_offset = 0;
        other.m_committed = 0;
        other.m_reserved = 0;
        other.m_last_alloc_end = 0;
        other.m_dtor_head = nullptr;
    }
    return *this;
}

void Arena::init_allocator() noexcept {
    m_allocator.ctx = this;
    m_allocator.alloc = [](void* ctx, usize size, usize align) noexcept -> void* {
        return static_cast<Arena*>(ctx)->alloc(size, align);
    };
    m_allocator.realloc = [](void* ctx, void* ptr, usize old_size, usize new_size, usize align) noexcept -> void* {
        return static_cast<Arena*>(ctx)->realloc(ptr, old_size, new_size, align);
    };
    m_allocator.free = [](void* ctx, void* ptr, usize size, usize) noexcept {
        Arena* self = static_cast<Arena*>(ctx);
        if (ptr && static_cast<u8*>(ptr) + size == self->m_base + self->m_offset) {
            self->m_offset -= size;
            self->m_last_alloc_end = self->m_offset;
        }
    };
}

void* Arena::alloc(usize size, usize align) {
    KEEL_ASSERT(align > 0 && (align & (align - 1)) == 0);
    if (size == 0) return nullptr;

    const usize aligned_offset = align_up(m_offset, align);
    const usize new_offset = aligned_offset + size;
    if (!m_base || new_offset > m_reserved) [[unlikely]] {
#if defined(__EMSCRIPTEN__)
        // Grow linear buffer if still empty
        if (m_offset == 0) {
            const usize next_cap = align_up(max(new_offset, m_reserved * 2), kCommitGranularity);
            u8* next_base = static_cast<u8*>(std::realloc(m_base, next_cap));
            if (next_base) {
                m_base = next_base;
                m_committed = next_cap;
                m_reserved = next_cap;
            } else {
                handle_oom(size, align);
            }
        } else {
            handle_oom(size, align);
        }
#else
        handle_oom(size, align);
#endif
    }

    if (new_offset > m_committed) {
        const usize target_commit = min(align_up(new_offset, kCommitGranularity), m_reserved);
        const usize commit_bytes = target_commit - m_committed;
#if defined(_WIN32)
        if (!VirtualAlloc(m_base + m_committed, static_cast<SIZE_T>(commit_bytes), MEM_COMMIT, PAGE_READWRITE)) {
            handle_oom(size, align);
        }
#elif !defined(__EMSCRIPTEN__)
        if (mprotect(m_base + m_committed, commit_bytes, PROT_READ | PROT_WRITE) != 0) {
            handle_oom(size, align);
        }
#endif
        m_committed = target_commit;
    }

    m_offset = new_offset;
    m_last_alloc_end = new_offset;
    return m_base + aligned_offset;
}

bool Arena::try_extend(void* ptr, usize old_size, usize new_size) noexcept {
    if (!ptr || !m_base) return false;
    u8* byte_ptr = static_cast<u8*>(ptr);
    if (byte_ptr < m_base || byte_ptr + old_size != m_base + m_offset) {
        return false;
    }
    if (new_size <= old_size) {
        m_offset = static_cast<usize>(byte_ptr - m_base) + new_size;
        m_last_alloc_end = m_offset;
        return true;
    }
    const usize extra = new_size - old_size;
    const usize new_offset = m_offset + extra;
    if (new_offset > m_reserved) {
        return false;
    }
    if (new_offset > m_committed) {
        const usize target_commit = min(align_up(new_offset, kCommitGranularity), m_reserved);
        const usize commit_bytes = target_commit - m_committed;
#if defined(_WIN32)
        if (!VirtualAlloc(m_base + m_committed, static_cast<SIZE_T>(commit_bytes), MEM_COMMIT, PAGE_READWRITE)) {
            return false;
        }
#elif !defined(__EMSCRIPTEN__)
        if (mprotect(m_base + m_committed, commit_bytes, PROT_READ | PROT_WRITE) != 0) {
            return false;
        }
#endif
        m_committed = target_commit;
    }
    m_offset = new_offset;
    m_last_alloc_end = new_offset;
    return true;
}

void* Arena::realloc(void* ptr, usize old_size, usize new_size, usize align) {
    if (!ptr) return alloc(new_size, align);
    if (new_size == 0) {
        (void)try_extend(ptr, old_size, 0);
        return nullptr;
    }

    // In-place extension/shrinking when `ptr` is the most recent allocation at the top of the arena
    if ((reinterpret_cast<usize>(ptr) & (align - 1)) == 0 && try_extend(ptr, old_size, new_size)) {
        return ptr;
    }

    void* new_ptr = alloc(new_size, align);
    if (old_size > 0) {
        std::memcpy(new_ptr, ptr, min(old_size, new_size));
    }
    return new_ptr;
}

void Arena::run_destructors_from(usize offset) noexcept {
    while (m_dtor_head && m_dtor_head->offset >= offset) {
        DtorNode* node = m_dtor_head;
        m_dtor_head = node->next;
        node->destroy(node->obj);
    }
}

void Arena::reset() noexcept {
    run_destructors_from(0);
    m_offset = 0;
    m_last_alloc_end = 0;
}

void Arena::rewind_to(usize offset) noexcept {
    KEEL_ASSERT(offset <= m_offset);
    run_destructors_from(offset);
    m_offset = offset;
    m_last_alloc_end = offset;
}

void Arena::release_memory() noexcept {
    run_destructors_from(0);
    if (m_base) {
#if defined(_WIN32)
        VirtualFree(m_base, 0, MEM_RELEASE);
#elif defined(__EMSCRIPTEN__)
        std::free(m_base);
#else
        munmap(m_base, m_reserved);
#endif
        m_base = nullptr;
    }
    m_offset = 0;
    m_committed = 0;
    m_reserved = 0;
    m_last_alloc_end = 0;
}

namespace {
struct ScratchPool {
    Arena arenas[2];
};

ScratchPool& thread_scratch_pool() noexcept {
    thread_local ScratchPool pool;
    return pool;
}
} // namespace

ArenaTemp get_scratch(Arena* const* conflicts, usize conflict_count) noexcept {
    ScratchPool& pool = thread_scratch_pool();
    for (int i = 0; i < 2; ++i) {
        Arena* candidate = &pool.arenas[i];
        bool has_conflict = false;
        for (usize c = 0; c < conflict_count; ++c) {
            if (conflicts[c] == candidate) {
                has_conflict = true;
                break;
            }
        }
        if (!has_conflict) {
            return candidate->begin_temp();
        }
    }
    return pool.arenas[0].begin_temp();
}

} // namespace keel
