#pragma once

#include "keel/core.h"

namespace keel {

namespace detail {
template <typename T> inline constexpr bool IsNullableCallable = IsPointer<T>;

template <typename T> [[nodiscard]] constexpr bool is_null_callable(const T& fn) noexcept {
    if constexpr (IsNullableCallable<RemoveCvRef<T>>) {
        return !static_cast<bool>(fn);
    } else {
        return false;
    }
}
} // namespace detail

template <typename Sig> class FunctionRef;

// Non-owning 2-pointer callable view (16 bytes on 64-bit, zero allocations).
template <typename R, typename... Args> class FunctionRef<R(Args...)> {
public:
    constexpr FunctionRef() noexcept = default;
    constexpr FunctionRef(decltype(nullptr)) noexcept {}

    constexpr FunctionRef(R (*fn)(Args...)) noexcept {
        if (fn) {
            m_storage.exact_fn = fn;
            m_trampoline = [](Storage s, Args... args) -> R {
                return s.exact_fn(keel::forward<Args>(args)...);
            };
        }
    }

    constexpr FunctionRef(R (*fn)(Args...) noexcept) noexcept
        : FunctionRef(static_cast<R (*)(Args...)>(fn)) {}

    template <typename Fp>
        requires(IsFunction<Fp>) && (!IsSame<Fp*, R (*)(Args...)>) && (!IsSame<Fp*, R (*)(Args...) noexcept>) &&
                requires(Fp* fp, Args&&... args) {
                    { static_cast<R>(fp(keel::forward<Args>(args)...)) };
                }
    FunctionRef(Fp* fn) noexcept {
        if (fn) {
            m_storage.generic_fn = reinterpret_cast<void (*)()>(fn);
            m_trampoline = [](Storage s, Args... args) -> R {
                return static_cast<R>(reinterpret_cast<Fp*>(s.generic_fn)(keel::forward<Args>(args)...));
            };
        }
    }

    template <typename F>
        requires(!IsSame<RemoveCvRef<F>, FunctionRef>) && (!IsSame<RemoveCvRef<F>, decltype(nullptr)>) &&
                (!IsFunction<RemoveReference<F>>) &&
                (!(IsPointer<RemoveCvRef<F>> && IsFunction<RemovePointer<RemoveCvRef<F>>>)) &&
                requires(RemoveReference<F>& f, Args&&... args) {
                    { static_cast<R>(f(keel::forward<Args>(args)...)) };
                }
    constexpr FunctionRef(F&& callable KEEL_LIFETIMEBOUND) noexcept {
        if (!detail::is_null_callable(callable)) {
            m_storage.obj = const_cast<void*>(static_cast<const void*>(&callable));
            m_trampoline = [](Storage s, Args... args) -> R {
                return static_cast<R>((*static_cast<RemoveReference<F>*>(s.obj))(keel::forward<Args>(args)...));
            };
        }
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_trampoline != nullptr; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_trampoline == nullptr; }

    constexpr R operator()(Args... args) const {
        KEEL_ASSERT(m_trampoline != nullptr);
        return m_trampoline(m_storage, keel::forward<Args>(args)...);
    }

    [[nodiscard]] friend constexpr bool operator==(FunctionRef f, decltype(nullptr)) noexcept { return f.empty(); }
    [[nodiscard]] friend constexpr bool operator!=(FunctionRef f, decltype(nullptr)) noexcept { return !f.empty(); }

private:
    union Storage {
        void* obj = nullptr;
        R (*exact_fn)(Args...);
        void (*generic_fn)();
    };

    using Trampoline = R (*)(Storage, Args...);
    Storage m_storage{};
    Trampoline m_trampoline = nullptr;
};

static_assert(sizeof(FunctionRef<void()>) == 2 * sizeof(void*), "FunctionRef must be exactly 2 pointers");

} // namespace keel
