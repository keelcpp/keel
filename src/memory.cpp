#include "keel/memory.h"
#include "keel/arena.h"
#include "keel/io.h"

#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include "win32_compat.h"
#include <malloc.h>
#elif !defined(__EMSCRIPTEN__)
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace keel {

void memory_trim() noexcept {
#if defined(_WIN32)
    (void)_heapmin();
#elif defined(__GLIBC__)
    (void)::malloc_trim(0);
#endif
}

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

i32 mem_cmp(const void* a, const void* b, usize bytes) noexcept {
    if (bytes == 0) return 0;
    return as<i32>(std::memcmp(a, b, bytes));
}

namespace {

template <typename T> struct AtomicSlot {
    T m_val;

    [[nodiscard]] T load_acquire() const noexcept {
#if defined(__clang__) || defined(__GNUC__)
        return __atomic_load_n(&m_val, __ATOMIC_ACQUIRE);
#elif defined(_WIN32)
        T v = *static_cast<const volatile T*>(&m_val);
        MemoryBarrier();
        return v;
#else
        return m_val;
#endif
    }

    void store_release(T v) noexcept {
#if defined(__clang__) || defined(__GNUC__)
        __atomic_store_n(&m_val, v, __ATOMIC_RELEASE);
#elif defined(_WIN32)
        MemoryBarrier();
        *static_cast<volatile T*>(&m_val) = v;
        MemoryBarrier();
#else
        m_val = v;
#endif
    }
};

constinit AtomicSlot<OomHandler> g_oom_handler{nullptr};
constinit AtomicSlot<void*> g_oom_user_data{nullptr};

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
    if (new_ptr) {
        if (old_size > 0) {
            const usize copy_bytes = min(old_size, new_size);
            std::memcpy(new_ptr, ptr, copy_bytes);
        }
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

constinit AtomicSlot<Allocator*> g_default_allocator{&g_system_allocator};

constexpr usize kCommitGranularity = 64 * 1024; // Commit in 64 KB chunks

bool checked_align_up(usize val, usize align, usize& out) noexcept {
    const usize mask = align - 1;
    if (val > (~usize{0}) - mask) [[unlikely]] {
        return false;
    }
    out = (val + mask) & ~mask;
    return true;
}

} // namespace

void set_oom_handler(OomHandler handler, void* user_data) noexcept {
    g_oom_user_data.store_release(user_data);
    g_oom_handler.store_release(handler);
}

[[noreturn]] void handle_oom(usize size, usize align) {
    OomHandler handler = g_oom_handler.load_acquire();
    if (handler) {
        handler(size, align, g_oom_user_data.load_acquire());
    }
    stderr_write("keel: out of memory\n");
    stderr_flush();
    std::abort();
}

Allocator* default_allocator() noexcept { return g_default_allocator.load_acquire(); }

void set_default_allocator(Allocator* alloc) noexcept {
    g_default_allocator.store_release(alloc ? alloc : &g_system_allocator);
}

// ============================================================================
// Arena
// ============================================================================

Arena::Arena(usize reserve_size, Allocator* upstream) noexcept : m_upstream(upstream) {
    init_allocator();
    if (reserve_size == 0) reserve_size = kDefaultReserveSize;
    if (!checked_align_up(reserve_size, kCommitGranularity, m_reserved)) {
        m_reserved = 0;
        return;
    }
#if defined(_WIN32)
    m_base = static_cast<u8*>(VirtualAlloc(nullptr, static_cast<SIZE_T>(m_reserved), MEM_RESERVE, PAGE_NOACCESS));
    if (!m_base) {
        m_reserved = 0;
    }
#elif defined(__EMSCRIPTEN__)
    // On 32-bit WebAssembly, virtual memory reservation without commit is not available;
    // allocate a page-aligned reserved arena block upfront so pointers and SIMD alignments remain valid.
    m_committed = m_reserved;
    void* aligned_ptr = nullptr;
    if (posix_memalign(&aligned_ptr, 4096, m_committed) == 0) {
        m_base = static_cast<u8*>(aligned_ptr);
    } else {
        m_base = nullptr;
        m_committed = 0;
        m_reserved = 0;
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
      m_last_alloc_end(other.m_last_alloc_end), m_overflow_used(other.m_overflow_used), m_dtor_head(other.m_dtor_head),
      m_overflow_head(other.m_overflow_head), m_upstream(other.m_upstream), m_has_bump_chunk(other.m_has_bump_chunk) {
    init_allocator();
    other.m_base = nullptr;
    other.m_offset = 0;
    other.m_committed = 0;
    other.m_reserved = 0;
    other.m_last_alloc_end = 0;
    other.m_overflow_used = 0;
    other.m_dtor_head = nullptr;
    other.m_overflow_head = nullptr;
    other.m_has_bump_chunk = false;
}

Arena& Arena::operator=(Arena&& other) noexcept {
    if (this != &other) {
        release_memory();
        m_base = other.m_base;
        m_offset = other.m_offset;
        m_committed = other.m_committed;
        m_reserved = other.m_reserved;
        m_last_alloc_end = other.m_last_alloc_end;
        m_overflow_used = other.m_overflow_used;
        m_dtor_head = other.m_dtor_head;
        m_overflow_head = other.m_overflow_head;
        m_upstream = other.m_upstream;
        m_has_bump_chunk = other.m_has_bump_chunk;
        other.m_base = nullptr;
        other.m_offset = 0;
        other.m_committed = 0;
        other.m_reserved = 0;
        other.m_last_alloc_end = 0;
        other.m_overflow_used = 0;
        other.m_dtor_head = nullptr;
        other.m_overflow_head = nullptr;
        other.m_has_bump_chunk = false;
    }
    return *this;
}

void Arena::init_allocator() noexcept {
    m_allocator.ctx = this;
    m_allocator.alloc = [](void* ctx, usize size, usize align) noexcept -> void* {
        return static_cast<Arena*>(ctx)->try_alloc(size, align);
    };
    m_allocator.realloc = [](void* ctx, void* ptr, usize old_size, usize new_size, usize align) noexcept -> void* {
        return static_cast<Arena*>(ctx)->try_realloc(ptr, old_size, new_size, align);
    };
    m_allocator.free = [](void* ctx, void* ptr, usize size, usize) noexcept {
        Arena* self = static_cast<Arena*>(ctx);
        if (!ptr || size == 0) return;
        if (!self->m_has_bump_chunk) {
            if (self->m_base && static_cast<u8*>(ptr) + size == self->m_base + self->m_offset) {
                self->m_offset -= size;
                self->m_last_alloc_end = self->m_offset;
            }
        } else if (self->m_overflow_head && self->m_overflow_head->is_bump_chunk) {
            u8* base = self->m_overflow_head->data;
            if (static_cast<u8*>(ptr) > base &&
                static_cast<u8*>(ptr) + size == base + self->m_overflow_head->chunk_used) {
                self->m_overflow_head->chunk_used -= size;
                self->m_overflow_used -= size;
            }
        }
    };
}

void* Arena::alloc_overflow(usize size, usize align) noexcept {
    constexpr usize kMinOverflowChunkSize = 1024 * 1024; // 1 MiB chunk when bumping in overflow mode
    Allocator* upstream = m_upstream ? m_upstream : default_allocator();
    const bool is_bump = m_has_bump_chunk || !m_base || (m_reserved - m_offset) <= kCommitGranularity;
    const usize payload_cap = is_bump ? max(size, kMinOverflowChunkSize) : size;
    const usize block_align = max(align, max(kDefaultAlign, alignof(OverflowBlock)));

    usize data_offset = 0;
    if (!checked_align_up(sizeof(OverflowBlock), align, data_offset)) [[unlikely]] {
        return nullptr;
    }
    if (payload_cap > (~usize{0}) - data_offset) [[unlikely]] {
        return nullptr;
    }
    if (size > (~usize{0}) - m_overflow_used) [[unlikely]] {
        return nullptr;
    }
    if (m_offset > (~usize{0}) - (m_overflow_used + size)) [[unlikely]] {
        return nullptr;
    }

    const usize total_alloc = data_offset + payload_cap;
    u8* raw = static_cast<u8*>(upstream->try_alloc_bytes(total_alloc, block_align));
    if (!raw) [[unlikely]] {
        return nullptr;
    }

    auto* blk = ::new (raw) OverflowBlock{};
    blk->prev = m_overflow_head;
    blk->data = raw + data_offset;
    blk->chunk_used = size;
    blk->chunk_cap = payload_cap;
    blk->birth_slab_offset = m_offset;
    blk->birth_overflow_used = m_overflow_used;
    blk->alloc_bytes = total_alloc;
    blk->alloc_align = block_align;
    blk->is_bump_chunk = is_bump;

    m_overflow_head = blk;
    m_overflow_used += size;
    if (is_bump) {
        m_has_bump_chunk = true;
    }
    return blk->data;
}

void* Arena::try_alloc(usize size, usize align) noexcept {
    KEEL_ASSERT(align > 0 && (align & (align - 1)) == 0);
    if (size == 0) return nullptr;

    if (!m_has_bump_chunk && m_base != nullptr) {
        usize aligned_addr = 0;
        const usize base_addr = reinterpret_cast<usize>(m_base);
        if (base_addr <= (~usize{0}) - m_offset &&
            checked_align_up(base_addr + m_offset, align, aligned_addr)) {
            const usize aligned_offset = aligned_addr - base_addr;
            if (size <= m_reserved && aligned_offset <= m_reserved - size) {
                const usize new_offset = aligned_offset + size;
                if (new_offset <= (~usize{0}) - m_overflow_used) {
                    if (new_offset > m_committed) {
                        usize aligned_commit = new_offset;
                        if (!checked_align_up(new_offset, kCommitGranularity, aligned_commit)) {
                            aligned_commit = m_reserved;
                        }
                        const usize target_commit = min(aligned_commit, m_reserved);
#if defined(_WIN32)
                        const usize commit_bytes = target_commit - m_committed;
                        if (!VirtualAlloc(m_base + m_committed, static_cast<SIZE_T>(commit_bytes), MEM_COMMIT,
                                          PAGE_READWRITE)) [[unlikely]] {
                            return nullptr;
                        }
#elif !defined(__EMSCRIPTEN__)
                        const usize commit_bytes = target_commit - m_committed;
                        if (mprotect(m_base + m_committed, commit_bytes, PROT_READ | PROT_WRITE) != 0) [[unlikely]] {
                            return nullptr;
                        }
#endif
                        m_committed = target_commit;
                    }
                    m_offset = new_offset;
                    m_last_alloc_end = new_offset;
                    return m_base + aligned_offset;
                }
            }
        }
    } else if (m_has_bump_chunk && m_overflow_head != nullptr && m_overflow_head->is_bump_chunk) {
        usize aligned_addr = 0;
        const usize base_addr = reinterpret_cast<usize>(m_overflow_head->data);
        if (base_addr <= (~usize{0}) - m_overflow_head->chunk_used &&
            checked_align_up(base_addr + m_overflow_head->chunk_used, align, aligned_addr)) {
            const usize aligned_offset = aligned_addr - base_addr;
            if (size <= m_overflow_head->chunk_cap && aligned_offset <= m_overflow_head->chunk_cap - size) {
                const usize new_chunk_used = aligned_offset + size;
                const usize delta = new_chunk_used - m_overflow_head->chunk_used;
                if (delta <= (~usize{0}) - m_overflow_used &&
                    m_offset <= (~usize{0}) - (m_overflow_used + delta)) {
                    m_overflow_head->chunk_used = new_chunk_used;
                    m_overflow_used += delta;
                    return m_overflow_head->data + aligned_offset;
                }
            }
        }
    }

    return alloc_overflow(size, align);
}

void* Arena::alloc(usize size, usize align) {
    KEEL_ASSERT(align > 0 && (align & (align - 1)) == 0);
    if (size == 0) return nullptr;
    void* p = try_alloc(size, align);
    if (!p) [[unlikely]] {
        handle_oom(size, align);
    }
    return p;
}

bool Arena::try_extend(void* ptr, usize old_size, usize new_size) noexcept {
    if (!ptr) return false;
    u8* byte_ptr = static_cast<u8*>(ptr);

    if (!m_has_bump_chunk) {
        if (!m_base) return false;
        if (byte_ptr < m_base || byte_ptr + old_size != m_base + m_offset) {
            return false;
        }
        if (new_size <= old_size) {
            m_offset = as<usize>(byte_ptr - m_base) + new_size;
            m_last_alloc_end = m_offset;
            return true;
        }
        const usize extra = new_size - old_size;
        if (extra > (~usize{0}) - m_offset) {
            return false;
        }
        const usize new_offset = m_offset + extra;
        if (new_offset > m_reserved || new_offset > (~usize{0}) - m_overflow_used) {
            return false;
        }
        if (new_offset > m_committed) {
            usize aligned_commit = new_offset;
            if (!checked_align_up(new_offset, kCommitGranularity, aligned_commit)) {
                aligned_commit = m_reserved;
            }
            const usize target_commit = min(aligned_commit, m_reserved);
#if defined(_WIN32)
            const usize commit_bytes = target_commit - m_committed;
            if (!VirtualAlloc(m_base + m_committed, static_cast<SIZE_T>(commit_bytes), MEM_COMMIT, PAGE_READWRITE)) {
                return false;
            }
#elif !defined(__EMSCRIPTEN__)
            const usize commit_bytes = target_commit - m_committed;
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

    if (m_overflow_head && m_overflow_head->is_bump_chunk) {
        u8* base = m_overflow_head->data;
        if (byte_ptr < base || byte_ptr + old_size != base + m_overflow_head->chunk_used) {
            return false;
        }
        if (new_size <= old_size) {
            const usize new_chunk_used = as<usize>(byte_ptr - base) + new_size;
            if (new_chunk_used == 0) {
                return false;
            }
            const usize shrink = m_overflow_head->chunk_used - new_chunk_used;
            m_overflow_head->chunk_used = new_chunk_used;
            m_overflow_used -= shrink;
            return true;
        }
        const usize extra = new_size - old_size;
        if (extra > m_overflow_head->chunk_cap - m_overflow_head->chunk_used) {
            return false;
        }
        if (extra > (~usize{0}) - m_overflow_used || m_offset > (~usize{0}) - (m_overflow_used + extra)) {
            return false;
        }
        m_overflow_head->chunk_used += extra;
        m_overflow_used += extra;
        return true;
    }

    return false;
}

void* Arena::try_realloc(void* ptr, usize old_size, usize new_size, usize align) noexcept {
    if (!ptr) return try_alloc(new_size, align);
    if (new_size == 0) {
        (void)try_extend(ptr, old_size, 0);
        return nullptr;
    }

    if ((reinterpret_cast<usize>(ptr) & (align - 1)) == 0 && try_extend(ptr, old_size, new_size)) {
        return ptr;
    }

    void* new_ptr = try_alloc(new_size, align);
    if (new_ptr && old_size > 0) {
        std::memcpy(new_ptr, ptr, min(old_size, new_size));
    }
    return new_ptr;
}

void* Arena::realloc(void* ptr, usize old_size, usize new_size, usize align) {
    if (!ptr) return alloc(new_size, align);
    if (new_size == 0) {
        (void)try_extend(ptr, old_size, 0);
        return nullptr;
    }
    void* new_ptr = try_realloc(ptr, old_size, new_size, align);
    if (!new_ptr) [[unlikely]] {
        handle_oom(new_size, align);
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

void Arena::free_overflow_from(usize offset) noexcept {
    Allocator* upstream = m_upstream ? m_upstream : default_allocator();
    while (m_overflow_head && m_overflow_head->birth_mark() >= offset) {
        OverflowBlock* blk = m_overflow_head;
        m_overflow_head = blk->prev;
        m_overflow_used = blk->birth_overflow_used;
        const usize bytes = blk->alloc_bytes;
        const usize align = blk->alloc_align;
        blk->~OverflowBlock();
        upstream->free_bytes(blk, bytes, align);
    }
    if (m_overflow_head && m_overflow_head->is_bump_chunk) {
        m_offset = m_overflow_head->birth_slab_offset;
        const usize new_chunk_used = offset - m_offset - m_overflow_head->birth_overflow_used;
        m_overflow_head->chunk_used = new_chunk_used;
        m_overflow_used = m_overflow_head->birth_overflow_used + new_chunk_used;
        m_has_bump_chunk = true;
    } else {
        m_has_bump_chunk = false;
        m_offset = offset - m_overflow_used;
    }
    m_last_alloc_end = m_offset;
}

void Arena::reset() noexcept { rewind_to(0); }

void Arena::rewind_to(usize offset) noexcept {
    KEEL_ASSERT(offset <= used());
    run_destructors_from(offset);
    free_overflow_from(offset);
}

void Arena::release_memory() noexcept {
    rewind_to(0);
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
    m_overflow_used = 0;
    m_has_bump_chunk = false;
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
    KEEL_ASSERT(false && "all thread-local scratch arenas are in conflict");
    return pool.arenas[0].begin_temp();
}

ArenaTemp get_scratch(const Allocator* conflict_alloc) noexcept {
    if (conflict_alloc && conflict_alloc->ctx) {
        Arena* conflict = static_cast<Arena*>(conflict_alloc->ctx);
        return get_scratch(&conflict, 1);
    }
    return get_scratch(nullptr, 0);
}

} // namespace keel
