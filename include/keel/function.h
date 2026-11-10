#pragma once

#include "keel/function_ref.h"
#include "keel/memory.h"

namespace keel {

template <typename Sig, usize N = 32> class FixedFunction;
template <typename Sig, usize N = 32> class SmallFunction;
template <typename Sig> class Function;

namespace detail {
template <typename Sig, usize N> inline constexpr bool IsNullableCallable<FixedFunction<Sig, N>> = true;
template <typename Sig, usize N> inline constexpr bool IsNullableCallable<SmallFunction<Sig, N>> = true;
template <typename Sig> inline constexpr bool IsNullableCallable<Function<Sig>> = true;

struct RawAllocGuard {
    Allocator* alloc = nullptr;
    void* ptr = nullptr;
    usize size = 0;
    usize align = 0;

    ~RawAllocGuard() noexcept {
        if (ptr) {
            alloc->free_bytes(ptr, size, align);
        }
    }

    void disarm() noexcept { ptr = nullptr; }
};
} // namespace detail

// Move-only owning callable with fixed inline buffer N bytes and zero heap allocations.
template <typename R, typename... Args, usize N> class FixedFunction<R(Args...), N> {
    static_assert(N >= sizeof(void*), "FixedFunction inline buffer must hold at least one pointer");

public:
    FixedFunction() noexcept = default;
    FixedFunction(decltype(nullptr)) noexcept {}

    template <typename F>
        requires(!IsSame<RemoveCvRef<F>, FixedFunction>) && (!IsSame<RemoveCvRef<F>, decltype(nullptr)>) &&
                requires(Decay<F>& f, Args&&... args) {
                    { static_cast<R>(f(keel::forward<Args>(args)...)) };
                }
    FixedFunction(F&& callable) noexcept(IsNothrowMoveConstructible<Decay<F>> && IsConstructible<Decay<F>, F&&>) {
        init_from(keel::forward<F>(callable));
    }

    ~FixedFunction() noexcept { reset(); }

    FixedFunction(const FixedFunction&) = delete;
    FixedFunction& operator=(const FixedFunction&) = delete;

    FixedFunction(FixedFunction&& other) noexcept {
        if (other.m_ops) {
            other.m_ops->move_and_destroy(m_storage, other.m_storage);
            m_ops = other.m_ops;
            other.m_ops = nullptr;
        }
    }

    FixedFunction& operator=(FixedFunction&& other) noexcept {
        if (this != &other) {
            reset();
            if (other.m_ops) {
                other.m_ops->move_and_destroy(m_storage, other.m_storage);
                m_ops = other.m_ops;
                other.m_ops = nullptr;
            }
        }
        return *this;
    }

    FixedFunction& operator=(decltype(nullptr)) noexcept {
        reset();
        return *this;
    }

    template <typename F>
        requires(!IsSame<RemoveCvRef<F>, FixedFunction>) && (!IsSame<RemoveCvRef<F>, decltype(nullptr)>) &&
                requires(Decay<F>& f, Args&&... args) {
                    { static_cast<R>(f(keel::forward<Args>(args)...)) };
                }
    FixedFunction& operator=(F&& callable) {
        reset();
        init_from(keel::forward<F>(callable));
        return *this;
    }

    void reset() noexcept {
        if (m_ops) {
            m_ops->destroy(m_storage);
            m_ops = nullptr;
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_ops != nullptr; }
    [[nodiscard]] bool empty() const noexcept { return m_ops == nullptr; }
    [[nodiscard]] static constexpr usize capacity() noexcept { return N; }

    R operator()(Args... args) const {
        KEEL_ASSERT(m_ops != nullptr);
        return m_ops->invoke(const_cast<u8*>(m_storage), keel::forward<Args>(args)...);
    }

    [[nodiscard]] friend bool operator==(const FixedFunction& f, decltype(nullptr)) noexcept { return f.empty(); }
    [[nodiscard]] friend bool operator!=(const FixedFunction& f, decltype(nullptr)) noexcept { return !f.empty(); }

private:
    struct Ops {
        R (*invoke)(u8* storage, Args... args);
        void (*move_and_destroy)(u8* dst, u8* src) noexcept;
        void (*destroy)(u8* storage) noexcept;
    };

    template <typename F> void init_from(F&& callable) {
        using Decayed = Decay<F>;
        static_assert(sizeof(Decayed) <= N, "Callable capture exceeds FixedFunction<Sig, N> inline buffer size");
        static_assert(alignof(Decayed) <= alignof(std::max_align_t), "Callable alignment exceeds max_align_t");
        static_assert(IsNothrowMoveConstructible<Decayed>, "Callable must be noexcept move constructible");
        static_assert(IsNothrowDestructible<Decayed>, "Callable must be noexcept destructible");

        if (detail::is_null_callable(callable)) {
            return;
        }
        ::new (m_storage) Decayed(keel::forward<F>(callable));
        m_ops = &ops_for<Decayed>();
    }

    template <typename Decayed> static const Ops& ops_for() noexcept {
        static constexpr Ops kTable = {
            [](u8* storage, Args... args) -> R {
                return static_cast<R>((*reinterpret_cast<Decayed*>(storage))(keel::forward<Args>(args)...));
            },
            [](u8* dst, u8* src) noexcept {
                Decayed* s = reinterpret_cast<Decayed*>(src);
                ::new (dst) Decayed(keel::move(*s));
                s->~Decayed();
            },
            [](u8* storage) noexcept { reinterpret_cast<Decayed*>(storage)->~Decayed(); },
        };
        return kTable;
    }

    alignas(std::max_align_t) u8 m_storage[N]{};
    const Ops* m_ops = nullptr;
};

// Small-buffer optimized move-only callable: stores captures <= N bytes inline (`m_heap == nullptr`),
// and spills larger or over-aligned captures to `Allocator*`.
template <typename R, typename... Args, usize N> class SmallFunction<R(Args...), N> {
    static_assert(N >= sizeof(void*), "SmallFunction inline buffer must hold at least one pointer");

public:
    SmallFunction() noexcept : m_alloc(default_allocator()) {}
    SmallFunction(decltype(nullptr), Allocator* alloc = nullptr) noexcept
        : m_alloc(alloc ? alloc : default_allocator()) {}
    explicit SmallFunction(Allocator* alloc) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    template <typename F>
        requires(!IsSame<RemoveCvRef<F>, SmallFunction>) && (!IsSame<RemoveCvRef<F>, Allocator*>) &&
                (!IsSame<RemoveCvRef<F>, decltype(nullptr)>) && requires(Decay<F>& f, Args&&... args) {
                    { static_cast<R>(f(keel::forward<Args>(args)...)) };
                }
    SmallFunction(F&& callable, Allocator* alloc = nullptr) : m_alloc(alloc ? alloc : default_allocator()) {
        init_from(keel::forward<F>(callable));
    }

    ~SmallFunction() noexcept { reset(); }

    SmallFunction(const SmallFunction&) = delete;
    SmallFunction& operator=(const SmallFunction&) = delete;

    SmallFunction(SmallFunction&& other) noexcept : m_alloc(other.m_alloc) { move_from(other); }

    SmallFunction& operator=(SmallFunction&& other) noexcept {
        if (this != &other) {
            reset();
            m_alloc = other.m_alloc;
            move_from(other);
        }
        return *this;
    }

    SmallFunction& operator=(decltype(nullptr)) noexcept {
        reset();
        return *this;
    }

    template <typename F>
        requires(!IsSame<RemoveCvRef<F>, SmallFunction>) && (!IsSame<RemoveCvRef<F>, Allocator*>) &&
                (!IsSame<RemoveCvRef<F>, decltype(nullptr)>) && requires(Decay<F>& f, Args&&... args) {
                    { static_cast<R>(f(keel::forward<Args>(args)...)) };
                }
    SmallFunction& operator=(F&& callable) {
        reset();
        init_from(keel::forward<F>(callable));
        return *this;
    }

    void reset() noexcept {
        if (m_ops) {
            m_ops->destroy(m_heap ? m_heap : static_cast<void*>(m_inline), m_alloc);
            m_heap = nullptr;
            m_ops = nullptr;
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_ops != nullptr; }
    [[nodiscard]] bool empty() const noexcept { return m_ops == nullptr; }
    [[nodiscard]] bool is_inline() const noexcept { return m_heap == nullptr; }
    [[nodiscard]] static constexpr usize inline_capacity() noexcept { return N; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

    R operator()(Args... args) const {
        KEEL_ASSERT(m_ops != nullptr);
        void* target = m_heap ? m_heap : const_cast<u8*>(m_inline);
        return m_ops->invoke(target, keel::forward<Args>(args)...);
    }

    [[nodiscard]] friend bool operator==(const SmallFunction& f, decltype(nullptr)) noexcept { return f.empty(); }
    [[nodiscard]] friend bool operator!=(const SmallFunction& f, decltype(nullptr)) noexcept { return !f.empty(); }

private:
    struct Ops {
        R (*invoke)(void* target, Args... args);
        void (*move_inline)(u8* dst, u8* src) noexcept;
        void (*destroy)(void* target, Allocator* alloc) noexcept;
    };

    void move_from(SmallFunction& other) noexcept {
        if (!other.m_ops) return;
        if (other.m_heap) {
            m_heap = other.m_heap;
            m_ops = other.m_ops;
            other.m_heap = nullptr;
            other.m_ops = nullptr;
        } else {
            other.m_ops->move_inline(m_inline, other.m_inline);
            m_ops = other.m_ops;
            other.m_ops = nullptr;
        }
    }

    template <typename F> void init_from(F&& callable) {
        using Decayed = Decay<F>;
        static_assert(IsNothrowMoveConstructible<Decayed>, "Callable must be noexcept move constructible");
        static_assert(IsNothrowDestructible<Decayed>, "Callable must be noexcept destructible");

        if (detail::is_null_callable(callable)) {
            return;
        }

        constexpr bool kFitsInline =
            (sizeof(Decayed) <= N) && (alignof(Decayed) <= alignof(std::max_align_t));
        if constexpr (kFitsInline) {
            ::new (m_inline) Decayed(keel::forward<F>(callable));
            m_heap = nullptr;
            m_ops = &inline_ops_for<Decayed>();
        } else {
            void* mem = m_alloc->alloc_bytes(sizeof(Decayed), alignof(Decayed));
            detail::RawAllocGuard guard{m_alloc, mem, sizeof(Decayed), alignof(Decayed)};
            ::new (mem) Decayed(keel::forward<F>(callable));
            guard.disarm();
            m_heap = mem;
            m_ops = &heap_ops_for<Decayed>();
        }
    }

    template <typename Decayed> static const Ops& inline_ops_for() noexcept {
        static constexpr Ops kTable = {
            [](void* target, Args... args) -> R {
                return static_cast<R>((*static_cast<Decayed*>(target))(keel::forward<Args>(args)...));
            },
            [](u8* dst, u8* src) noexcept {
                Decayed* s = reinterpret_cast<Decayed*>(src);
                ::new (dst) Decayed(keel::move(*s));
                s->~Decayed();
            },
            [](void* target, Allocator*) noexcept { static_cast<Decayed*>(target)->~Decayed(); },
        };
        return kTable;
    }

    template <typename Decayed> static const Ops& heap_ops_for() noexcept {
        static constexpr Ops kTable = {
            [](void* target, Args... args) -> R {
                return static_cast<R>((*static_cast<Decayed*>(target))(keel::forward<Args>(args)...));
            },
            nullptr,
            [](void* target, Allocator* alloc) noexcept {
                static_cast<Decayed*>(target)->~Decayed();
                alloc->free_bytes(target, sizeof(Decayed), alignof(Decayed));
            },
        };
        return kTable;
    }

    void* m_heap = nullptr;
    const Ops* m_ops = nullptr;
    Allocator* m_alloc = nullptr;
    alignas(std::max_align_t) u8 m_inline[N]{};
};

// Move-only dynamic callable backed by `Allocator*` without inline SBO bloat (3 pointers).
template <typename R, typename... Args> class Function<R(Args...)> {
public:
    Function() noexcept : m_alloc(default_allocator()) {}
    Function(decltype(nullptr), Allocator* alloc = nullptr) noexcept
        : m_alloc(alloc ? alloc : default_allocator()) {}
    explicit Function(Allocator* alloc) noexcept : m_alloc(alloc ? alloc : default_allocator()) {}

    template <typename F>
        requires(!IsSame<RemoveCvRef<F>, Function>) && (!IsSame<RemoveCvRef<F>, Allocator*>) &&
                (!IsSame<RemoveCvRef<F>, decltype(nullptr)>) && requires(Decay<F>& f, Args&&... args) {
                    { static_cast<R>(f(keel::forward<Args>(args)...)) };
                }
    Function(F&& callable, Allocator* alloc = nullptr) : m_alloc(alloc ? alloc : default_allocator()) {
        init_from(keel::forward<F>(callable));
    }

    ~Function() noexcept { reset(); }

    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;

    Function(Function&& other) noexcept
        : m_ptr(other.m_ptr), m_ops(other.m_ops), m_alloc(other.m_alloc) {
        other.m_ptr = nullptr;
        other.m_ops = nullptr;
    }

    Function& operator=(Function&& other) noexcept {
        if (this != &other) {
            reset();
            m_ptr = other.m_ptr;
            m_ops = other.m_ops;
            m_alloc = other.m_alloc;
            other.m_ptr = nullptr;
            other.m_ops = nullptr;
        }
        return *this;
    }

    Function& operator=(decltype(nullptr)) noexcept {
        reset();
        return *this;
    }

    template <typename F>
        requires(!IsSame<RemoveCvRef<F>, Function>) && (!IsSame<RemoveCvRef<F>, Allocator*>) &&
                (!IsSame<RemoveCvRef<F>, decltype(nullptr)>) && requires(Decay<F>& f, Args&&... args) {
                    { static_cast<R>(f(keel::forward<Args>(args)...)) };
                }
    Function& operator=(F&& callable) {
        reset();
        init_from(keel::forward<F>(callable));
        return *this;
    }

    void reset() noexcept {
        if (m_ops) {
            m_ops->destroy(m_ptr, m_alloc);
            m_ptr = nullptr;
            m_ops = nullptr;
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_ops != nullptr; }
    [[nodiscard]] bool empty() const noexcept { return m_ops == nullptr; }
    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

    R operator()(Args... args) const {
        KEEL_ASSERT(m_ops != nullptr);
        return m_ops->invoke(m_ptr, keel::forward<Args>(args)...);
    }

    [[nodiscard]] friend bool operator==(const Function& f, decltype(nullptr)) noexcept { return f.empty(); }
    [[nodiscard]] friend bool operator!=(const Function& f, decltype(nullptr)) noexcept { return !f.empty(); }

private:
    struct Ops {
        R (*invoke)(void* ptr, Args... args);
        void (*destroy)(void* ptr, Allocator* alloc) noexcept;
    };

    template <typename F> void init_from(F&& callable) {
        using Decayed = Decay<F>;
        static_assert(IsNothrowMoveConstructible<Decayed>, "Callable must be noexcept move constructible");
        static_assert(IsNothrowDestructible<Decayed>, "Callable must be noexcept destructible");

        if (detail::is_null_callable(callable)) {
            return;
        }
        void* mem = m_alloc->alloc_bytes(sizeof(Decayed), alignof(Decayed));
        detail::RawAllocGuard guard{m_alloc, mem, sizeof(Decayed), alignof(Decayed)};
        ::new (mem) Decayed(keel::forward<F>(callable));
        guard.disarm();
        m_ptr = mem;
        m_ops = &ops_for<Decayed>();
    }

    template <typename Decayed> static const Ops& ops_for() noexcept {
        static constexpr Ops kTable = {
            [](void* ptr, Args... args) -> R {
                return static_cast<R>((*static_cast<Decayed*>(ptr))(keel::forward<Args>(args)...));
            },
            [](void* ptr, Allocator* alloc) noexcept {
                static_cast<Decayed*>(ptr)->~Decayed();
                alloc->free_bytes(ptr, sizeof(Decayed), alignof(Decayed));
            },
        };
        return kTable;
    }

    void* m_ptr = nullptr;
    const Ops* m_ops = nullptr;
    Allocator* m_alloc = nullptr;
};

static_assert(sizeof(Function<void()>) == 3 * sizeof(void*), "Function must be exactly 3 pointers");

} // namespace keel
