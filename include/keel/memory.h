#pragma once

#include "keel/core.h"

namespace keel {

inline constexpr isize kDefaultAlign = static_cast<isize>(alignof(std::max_align_t));

using OomHandler = void (*)(isize size, isize align, void* user_data);

void set_oom_handler(OomHandler handler, void* user_data = nullptr) noexcept;
[[noreturn]] void handle_oom(isize size, isize align);

struct Allocator {
    void* (*alloc)(void* ctx, isize size, isize align) noexcept = nullptr;
    void* (*realloc)(void* ctx, void* ptr, isize old_size, isize new_size, isize align) noexcept = nullptr;
    void (*free)(void* ctx, void* ptr, isize size, isize align) noexcept = nullptr;
    void* ctx = nullptr;

    [[nodiscard]] void* alloc_bytes(isize size, isize align = kDefaultAlign) {
        KEEL_ASSERT(size >= 0 && align > 0 && (align & (align - 1)) == 0);
        if (size == 0) return nullptr;
        void* p = alloc(ctx, size, align);
        if (!p) [[unlikely]] {
            handle_oom(size, align);
        }
        return p;
    }

    [[nodiscard]] void* realloc_bytes(void* ptr, isize old_size, isize new_size, isize align = kDefaultAlign) {
        KEEL_ASSERT(old_size >= 0 && new_size >= 0 && align > 0 && (align & (align - 1)) == 0);
        if (new_size == 0) {
            if (ptr) free(ctx, ptr, old_size, align);
            return nullptr;
        }
        if (!ptr) {
            return alloc_bytes(new_size, align);
        }
        void* p = realloc(ctx, ptr, old_size, new_size, align);
        if (!p) [[unlikely]] {
            handle_oom(new_size, align);
        }
        return p;
    }

    void free_bytes(void* ptr, isize size, isize align = kDefaultAlign) noexcept {
        if (ptr) {
            free(ctx, ptr, size, align);
        }
    }

    template <typename T> [[nodiscard]] T* alloc_array(isize count) {
        KEEL_ASSERT(count >= 0);
        if (count == 0) return nullptr;
        return static_cast<T*>(alloc_bytes(count * static_cast<isize>(sizeof(T)), static_cast<isize>(alignof(T))));
    }

    template <typename T> void free_array(T* ptr, isize count) noexcept {
        if (ptr) {
            free_bytes(ptr, count * static_cast<isize>(sizeof(T)), static_cast<isize>(alignof(T)));
        }
    }

    template <typename T, typename... Args> [[nodiscard]] T* create(Args&&... args) {
        void* mem = alloc_bytes(static_cast<isize>(sizeof(T)), static_cast<isize>(alignof(T)));
        return ::new (mem) T(forward<Args>(args)...);
    }

    template <typename T> void destroy(T* ptr) noexcept {
        if (ptr) {
            ptr->~T();
            free_bytes(ptr, static_cast<isize>(sizeof(T)), static_cast<isize>(alignof(T)));
        }
    }
};

[[nodiscard]] Allocator* default_allocator() noexcept;
void set_default_allocator(Allocator* alloc) noexcept;

// Raw memory primitives without <cstring> in public headers
void mem_copy(void* dst, const void* src, usize bytes) noexcept;
void mem_move(void* dst, const void* src, usize bytes) noexcept;
void mem_set(void* dst, u8 val, usize bytes) noexcept;
inline void mem_zero(void* dst, usize bytes) noexcept { mem_set(dst, 0, bytes); }
[[nodiscard]] int mem_cmp(const void* a, const void* b, usize bytes) noexcept;
[[nodiscard]] inline bool mem_eq(const void* a, const void* b, usize bytes) noexcept {
    return bytes == 0 || mem_cmp(a, b, bytes) == 0;
}

} // namespace keel
