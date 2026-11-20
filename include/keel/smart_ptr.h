#pragma once

#include "keel/hash.h"
#include "keel/memory.h"

#if defined(_MSC_VER) && !defined(__clang__)
extern "C" long _InterlockedIncrement(long volatile* addend);
extern "C" long _InterlockedDecrement(long volatile* addend);
#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#endif

namespace keel {

namespace detail {

#if defined(_MSC_VER) && !defined(__clang__)
using AtomicRefCount = volatile long;

inline u32 arc_inc(AtomicRefCount& refs) noexcept { return static_cast<u32>(_InterlockedIncrement(&refs)); }
inline u32 arc_dec(AtomicRefCount& refs) noexcept { return static_cast<u32>(_InterlockedDecrement(&refs)); }
inline u32 arc_load(const AtomicRefCount& refs) noexcept {
    // Volatile 32-bit aligned load on x86/x64/ARM64 MSVC is atomic.
    return static_cast<u32>(refs);
}
#else
using AtomicRefCount = u32;

inline u32 arc_inc(AtomicRefCount& refs) noexcept { return __atomic_add_fetch(&refs, 1u, __ATOMIC_RELAXED); }
inline u32 arc_dec(AtomicRefCount& refs) noexcept { return __atomic_sub_fetch(&refs, 1u, __ATOMIC_ACQ_REL); }
inline u32 arc_load(const AtomicRefCount& refs) noexcept { return __atomic_load_n(&refs, __ATOMIC_RELAXED); }
#endif

struct ArcControlBlock {
    AtomicRefCount refs = 1;
    Allocator* alloc = nullptr;
    void (*destroy_and_free)(ArcControlBlock* cb) noexcept = nullptr;
};

template <typename U> struct ArcInlineNode final : ArcControlBlock {
    alignas(U) u8 storage[sizeof(U)];

    [[nodiscard]] U* ptr() noexcept { return reinterpret_cast<U*>(storage); }
};

template <typename U> struct ArcExternalNode final : ArcControlBlock {
    U* raw_ptr = nullptr;
    Allocator* obj_alloc = nullptr;
    void (*obj_destroy)(void* raw, Allocator* alloc) noexcept = nullptr;
};

struct RawSmartPtrGuard {
    Allocator* alloc = nullptr;
    void* ptr = nullptr;
    usize size = 0;
    usize align = 0;

    ~RawSmartPtrGuard() noexcept {
        if (ptr) {
            alloc->free_bytes(ptr, size, align);
        }
    }

    void disarm() noexcept { ptr = nullptr; }
};

} // namespace detail

// Allocator-aware exclusive smart pointer supporting incomplete types and polymorphic base upcasting.
template <typename T> class UniquePtr {
public:
    constexpr UniquePtr() noexcept = default;
    constexpr UniquePtr(decltype(nullptr)) noexcept {}

    template <typename U>
        requires IsConvertible<U*, T*>
    explicit UniquePtr(U* ptr, Allocator* alloc = nullptr) noexcept
        : m_ptr(ptr),
          m_raw(const_cast<RemoveCv<U>*>(ptr)),
          m_alloc(ptr ? (alloc ? alloc : default_allocator()) : nullptr),
          m_destroy(ptr ? +[](void* raw, Allocator* a) noexcept {
              using CleanU = RemoveCv<U>;
              auto* typed = static_cast<CleanU*>(raw);
              typed->~CleanU();
              a->free_bytes(typed, sizeof(CleanU), alignof(CleanU));
          } : nullptr) {}

    [[nodiscard]] static UniquePtr from_raw(T* ptr, void (*custom_destroy)(void* raw, Allocator* alloc) noexcept,
                                            Allocator* alloc = nullptr) noexcept {
        UniquePtr out;
        if (ptr) {
            out.m_ptr = ptr;
            out.m_raw = const_cast<RemoveCv<T>*>(ptr);
            out.m_alloc = alloc;
            out.m_destroy = custom_destroy;
        }
        return out;
    }

    ~UniquePtr() noexcept { reset(); }

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& other) noexcept
        : m_ptr(other.m_ptr), m_raw(other.m_raw), m_alloc(other.m_alloc), m_destroy(other.m_destroy) {
        other.m_ptr = nullptr;
        other.m_raw = nullptr;
        other.m_alloc = nullptr;
        other.m_destroy = nullptr;
    }

    template <typename U>
        requires(!IsSame<U, T>) && IsConvertible<U*, T*>
    UniquePtr(UniquePtr<U>&& other) noexcept
        : m_ptr(other.m_ptr), m_raw(other.m_raw), m_alloc(other.m_alloc), m_destroy(other.m_destroy) {
        other.m_ptr = nullptr;
        other.m_raw = nullptr;
        other.m_alloc = nullptr;
        other.m_destroy = nullptr;
    }

    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            reset();
            m_ptr = other.m_ptr;
            m_raw = other.m_raw;
            m_alloc = other.m_alloc;
            m_destroy = other.m_destroy;
            other.m_ptr = nullptr;
            other.m_raw = nullptr;
            other.m_alloc = nullptr;
            other.m_destroy = nullptr;
        }
        return *this;
    }

    template <typename U>
        requires(!IsSame<U, T>) && IsConvertible<U*, T*>
    UniquePtr& operator=(UniquePtr<U>&& other) noexcept {
        reset();
        m_ptr = other.m_ptr;
        m_raw = other.m_raw;
        m_alloc = other.m_alloc;
        m_destroy = other.m_destroy;
        other.m_ptr = nullptr;
        other.m_raw = nullptr;
        other.m_alloc = nullptr;
        other.m_destroy = nullptr;
        return *this;
    }

    UniquePtr& operator=(decltype(nullptr)) noexcept {
        reset();
        return *this;
    }

    void reset() noexcept {
        if (m_destroy && m_raw) {
            void* raw = m_raw;
            Allocator* alloc = m_alloc;
            auto destroy_fn = m_destroy;
            m_ptr = nullptr;
            m_raw = nullptr;
            m_alloc = nullptr;
            m_destroy = nullptr;
            destroy_fn(raw, alloc);
        } else {
            m_ptr = nullptr;
            m_raw = nullptr;
            m_alloc = nullptr;
            m_destroy = nullptr;
        }
    }

    template <typename U>
        requires IsConvertible<U*, T*>
    void reset(U* ptr, Allocator* alloc = nullptr) noexcept {
        *this = UniquePtr<T>(ptr, alloc);
    }

    [[nodiscard]] T* release() noexcept {
        T* p = m_ptr;
        m_ptr = nullptr;
        m_raw = nullptr;
        m_alloc = nullptr;
        m_destroy = nullptr;
        return p;
    }

    [[nodiscard]] T* get() const noexcept { return m_ptr; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

    [[nodiscard]] T& operator*() const noexcept {
        KEEL_ASSERT(m_ptr != nullptr);
        return *m_ptr;
    }

    [[nodiscard]] T* operator->() const noexcept {
        KEEL_ASSERT(m_ptr != nullptr);
        return m_ptr;
    }

    [[nodiscard]] friend bool operator==(const UniquePtr& a, const UniquePtr& b) noexcept { return a.m_ptr == b.m_ptr; }
    [[nodiscard]] friend bool operator!=(const UniquePtr& a, const UniquePtr& b) noexcept { return a.m_ptr != b.m_ptr; }
    [[nodiscard]] friend bool operator==(const UniquePtr& a, decltype(nullptr)) noexcept { return a.m_ptr == nullptr; }
    [[nodiscard]] friend bool operator!=(const UniquePtr& a, decltype(nullptr)) noexcept { return a.m_ptr != nullptr; }
    [[nodiscard]] friend bool operator==(decltype(nullptr), const UniquePtr& b) noexcept { return b.m_ptr == nullptr; }
    [[nodiscard]] friend bool operator!=(decltype(nullptr), const UniquePtr& b) noexcept { return b.m_ptr != nullptr; }

private:
    template <typename> friend class UniquePtr;
    template <typename> friend class Arc;

    T* m_ptr = nullptr;
    void* m_raw = nullptr;
    Allocator* m_alloc = nullptr;
    void (*m_destroy)(void* raw, Allocator* alloc) noexcept = nullptr;
};

template <typename T, typename... Args> [[nodiscard]] UniquePtr<T> make_unique_in(Allocator* alloc, Args&&... args) {
    using CleanT = RemoveCv<T>;
    Allocator* a = alloc ? alloc : default_allocator();
    void* mem = a->alloc_bytes(sizeof(CleanT), alignof(CleanT));
    detail::RawSmartPtrGuard guard{a, mem, sizeof(CleanT), alignof(CleanT)};
    CleanT* obj = ::new (mem) CleanT(keel::forward<Args>(args)...);
    guard.disarm();
    return UniquePtr<T>(obj, a);
}

template <typename T, typename... Args> [[nodiscard]] UniquePtr<T> make_unique(Args&&... args) {
    return make_unique_in<T>(default_allocator(), keel::forward<Args>(args)...);
}

// Thread-safe atomic reference-counted smart pointer (single-allocation via make_arc<T>).
template <typename T> class Arc {
public:
    constexpr Arc() noexcept = default;
    constexpr Arc(decltype(nullptr)) noexcept {}

    template <typename U>
        requires IsConvertible<U*, T*>
    explicit Arc(U* ptr, Allocator* alloc = nullptr) {
        if (!ptr) return;
        using CleanU = RemoveCv<U>;
        using Node = detail::ArcExternalNode<CleanU>;
        Allocator* a = alloc ? alloc : default_allocator();
        void* mem = a->alloc_bytes(sizeof(Node), alignof(Node));
        auto* node = ::new (mem) Node{};
        node->refs = 1;
        node->alloc = a;
        node->raw_ptr = const_cast<CleanU*>(ptr);
        node->obj_alloc = a;
        node->obj_destroy = +[](void* raw, Allocator* oa) noexcept {
            auto* typed = static_cast<CleanU*>(raw);
            typed->~CleanU();
            oa->free_bytes(typed, sizeof(CleanU), alignof(CleanU));
        };
        node->destroy_and_free = +[](detail::ArcControlBlock* base) noexcept {
            auto* ext = static_cast<Node*>(base);
            if (ext->obj_destroy && ext->raw_ptr) {
                ext->obj_destroy(ext->raw_ptr, ext->obj_alloc);
            }
            Allocator* ca = ext->alloc;
            ext->~Node();
            ca->free_bytes(ext, sizeof(Node), alignof(Node));
        };
        m_ptr = ptr;
        m_cb = node;
    }

    template <typename U>
        requires IsConvertible<U*, T*>
    Arc(UniquePtr<U>&& unique) {
        if (!unique.m_ptr) return;
        using Node = detail::ArcExternalNode<void>;
        Allocator* a = unique.m_alloc ? unique.m_alloc : default_allocator();
        void* mem = a->alloc_bytes(sizeof(Node), alignof(Node));
        auto* node = ::new (mem) Node{};
        node->refs = 1;
        node->alloc = a;
        node->raw_ptr = unique.m_raw;
        node->obj_alloc = unique.m_alloc;
        node->obj_destroy = unique.m_destroy;
        node->destroy_and_free = +[](detail::ArcControlBlock* base) noexcept {
            auto* ext = static_cast<Node*>(base);
            if (ext->obj_destroy && ext->raw_ptr) {
                ext->obj_destroy(ext->raw_ptr, ext->obj_alloc);
            }
            Allocator* ca = ext->alloc;
            ext->~Node();
            ca->free_bytes(ext, sizeof(Node), alignof(Node));
        };
        m_ptr = unique.m_ptr;
        m_cb = node;
        unique.m_ptr = nullptr;
        unique.m_raw = nullptr;
        unique.m_alloc = nullptr;
        unique.m_destroy = nullptr;
    }

    ~Arc() noexcept { reset(); }

    Arc(const Arc& other) noexcept : m_ptr(other.m_ptr), m_cb(other.m_cb) {
        if (m_cb) {
            detail::arc_inc(m_cb->refs);
        }
    }

    template <typename U>
        requires(!IsSame<U, T>) && IsConvertible<U*, T*>
    Arc(const Arc<U>& other) noexcept : m_ptr(other.m_ptr), m_cb(other.m_cb) {
        if (m_cb) {
            detail::arc_inc(m_cb->refs);
        }
    }

    Arc(Arc&& other) noexcept : m_ptr(other.m_ptr), m_cb(other.m_cb) {
        other.m_ptr = nullptr;
        other.m_cb = nullptr;
    }

    template <typename U>
        requires(!IsSame<U, T>) && IsConvertible<U*, T*>
    Arc(Arc<U>&& other) noexcept : m_ptr(other.m_ptr), m_cb(other.m_cb) {
        other.m_ptr = nullptr;
        other.m_cb = nullptr;
    }

    Arc& operator=(const Arc& other) noexcept {
        if (this != &other) {
            if (other.m_cb) {
                detail::arc_inc(other.m_cb->refs);
            }
            reset();
            m_ptr = other.m_ptr;
            m_cb = other.m_cb;
        }
        return *this;
    }

    template <typename U>
        requires(!IsSame<U, T>) && IsConvertible<U*, T*>
    Arc& operator=(const Arc<U>& other) noexcept {
        if (other.m_cb) {
            detail::arc_inc(other.m_cb->refs);
        }
        reset();
        m_ptr = other.m_ptr;
        m_cb = other.m_cb;
        return *this;
    }

    Arc& operator=(Arc&& other) noexcept {
        if (this != &other) {
            reset();
            m_ptr = other.m_ptr;
            m_cb = other.m_cb;
            other.m_ptr = nullptr;
            other.m_cb = nullptr;
        }
        return *this;
    }

    template <typename U>
        requires(!IsSame<U, T>) && IsConvertible<U*, T*>
    Arc& operator=(Arc<U>&& other) noexcept {
        reset();
        m_ptr = other.m_ptr;
        m_cb = other.m_cb;
        other.m_ptr = nullptr;
        other.m_cb = nullptr;
        return *this;
    }

    Arc& operator=(decltype(nullptr)) noexcept {
        reset();
        return *this;
    }

    [[nodiscard]] Arc clone() const noexcept { return *this; }

    void reset() noexcept {
        if (m_cb) {
            detail::ArcControlBlock* cb = m_cb;
            m_ptr = nullptr;
            m_cb = nullptr;
            if (detail::arc_dec(cb->refs) == 0u) {
                cb->destroy_and_free(cb);
            }
        } else {
            m_ptr = nullptr;
        }
    }

    [[nodiscard]] T* get() const noexcept { return m_ptr; }
    [[nodiscard]] u32 use_count() const noexcept { return m_cb ? detail::arc_load(m_cb->refs) : 0u; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

    [[nodiscard]] T& operator*() const noexcept {
        KEEL_ASSERT(m_ptr != nullptr);
        return *m_ptr;
    }

    [[nodiscard]] T* operator->() const noexcept {
        KEEL_ASSERT(m_ptr != nullptr);
        return m_ptr;
    }

    [[nodiscard]] friend bool operator==(const Arc& a, const Arc& b) noexcept { return a.m_ptr == b.m_ptr; }
    [[nodiscard]] friend bool operator!=(const Arc& a, const Arc& b) noexcept { return a.m_ptr != b.m_ptr; }
    [[nodiscard]] friend bool operator==(const Arc& a, decltype(nullptr)) noexcept { return a.m_ptr == nullptr; }
    [[nodiscard]] friend bool operator!=(const Arc& a, decltype(nullptr)) noexcept { return a.m_ptr != nullptr; }
    [[nodiscard]] friend bool operator==(decltype(nullptr), const Arc& b) noexcept { return b.m_ptr == nullptr; }
    [[nodiscard]] friend bool operator!=(decltype(nullptr), const Arc& b) noexcept { return b.m_ptr != nullptr; }

private:
    template <typename> friend class Arc;

    template <typename U, typename... Args>
    friend Arc<U> make_arc_in(Allocator* alloc, Args&&... args);

    constexpr Arc(T* ptr, detail::ArcControlBlock* cb) noexcept : m_ptr(ptr), m_cb(cb) {}

    T* m_ptr = nullptr;
    detail::ArcControlBlock* m_cb = nullptr;
};

template <typename T, typename... Args> [[nodiscard]] Arc<T> make_arc_in(Allocator* alloc, Args&&... args) {
    using CleanT = RemoveCv<T>;
    using Node = detail::ArcInlineNode<CleanT>;
    Allocator* a = alloc ? alloc : default_allocator();
    void* mem = a->alloc_bytes(sizeof(Node), alignof(Node));
    detail::RawSmartPtrGuard guard{a, mem, sizeof(Node), alignof(Node)};
    auto* node = ::new (mem) Node{};
    node->refs = 1;
    node->alloc = a;
    node->destroy_and_free = +[](detail::ArcControlBlock* base) noexcept {
        auto* self = static_cast<Node*>(base);
        self->ptr()->~CleanT();
        Allocator* ca = self->alloc;
        self->~Node();
        ca->free_bytes(self, sizeof(Node), alignof(Node));
    };
    CleanT* obj = ::new (node->storage) CleanT(keel::forward<Args>(args)...);
    guard.disarm();
    return Arc<T>(obj, node);
}

template <typename T, typename... Args> [[nodiscard]] Arc<T> make_arc(Args&&... args) {
    return make_arc_in<T>(default_allocator(), keel::forward<Args>(args)...);
}

template <typename T> struct Hash<UniquePtr<T>> {
    [[nodiscard]] u64 operator()(const UniquePtr<T>& p) const noexcept { return Hash<T*>{}(p.get()); }
};

template <typename T> struct Hash<Arc<T>> {
    [[nodiscard]] u64 operator()(const Arc<T>& p) const noexcept { return Hash<T*>{}(p.get()); }
};

} // namespace keel
