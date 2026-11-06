#pragma once

#include "keel/core.h"

namespace keel {

template <typename Sig, usize N = 32> class Function;

// Move-only owning callable with fixed inline buffer N bytes and zero heap allocations.
template <typename R, typename... Args, usize N> class Function<R(Args...), N> {
    static_assert(N >= sizeof(void*), "Function inline buffer must hold at least one pointer");

public:
    Function() noexcept = default;
    Function(decltype(nullptr)) noexcept {}

    template <typename F>
        requires(!IsSame<RemoveCvRef<F>, Function>) && requires(RemoveCvRef<F>& f, Args&&... args) {
            { static_cast<R>(f(keel::forward<Args>(args)...)) };
        }
    Function(F&& callable) noexcept {
        using Decayed = RemoveCvRef<F>;
        static_assert(sizeof(Decayed) <= N, "Callable capture exceeds Function<Sig, N> inline buffer size");
        static_assert(alignof(Decayed) <= alignof(std::max_align_t), "Callable alignment exceeds max_align_t");
        static_assert(IsNothrowMoveConstructible<Decayed>, "Callable must be noexcept move constructible");
        static_assert(IsNothrowDestructible<Decayed>, "Callable must be noexcept destructible");

        ::new (m_storage) Decayed(keel::forward<F>(callable));
        m_ops = &ops_for<Decayed>();
    }

    ~Function() noexcept { reset(); }

    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;

    Function(Function&& other) noexcept {
        if (other.m_ops) {
            other.m_ops->move_and_destroy(m_storage, other.m_storage);
            m_ops = other.m_ops;
            other.m_ops = nullptr;
        }
    }

    Function& operator=(Function&& other) noexcept {
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

    Function& operator=(decltype(nullptr)) noexcept {
        reset();
        return *this;
    }

    void reset() noexcept {
        if (m_ops) {
            m_ops->destroy(m_storage);
            m_ops = nullptr;
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_ops != nullptr; }

    R operator()(Args... args) const {
        KEEL_ASSERT(m_ops != nullptr);
        return m_ops->invoke(const_cast<u8*>(m_storage), keel::forward<Args>(args)...);
    }

private:
    struct Ops {
        R (*invoke)(u8* storage, Args... args);
        void (*move_and_destroy)(u8* dst, u8* src) noexcept;
        void (*destroy)(u8* storage) noexcept;
    };

    template <typename Decayed> static const Ops& ops_for() noexcept {
        static constexpr Ops kTable = {
            [](u8* storage, Args... args) -> R {
                return (*reinterpret_cast<Decayed*>(storage))(keel::forward<Args>(args)...);
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

} // namespace keel
