#pragma once

#include "keel/core.h"

namespace keel {

template <typename Sig> class FunctionRef;

// Non-owning 2-pointer callable view (16 bytes on 64-bit, zero allocations).
template <typename R, typename... Args> class FunctionRef<R(Args...)> {
public:
    constexpr FunctionRef() noexcept = default;
    constexpr FunctionRef(decltype(nullptr)) noexcept {}

    template <typename F>
        requires(!IsSame<RemoveCvRef<F>, FunctionRef>) && requires(F&& f, Args&&... args) {
            { static_cast<R>(f(keel::forward<Args>(args)...)) };
        }
    constexpr FunctionRef(F&& callable KEEL_LIFETIMEBOUND) noexcept
        : m_obj(const_cast<void*>(static_cast<const void*>(&callable))),
          m_trampoline([](void* obj, Args... args) -> R {
              return (*static_cast<RemoveReference<F>*>(obj))(keel::forward<Args>(args)...);
          }) {}

    constexpr FunctionRef(R (*fn)(Args...)) noexcept
        : m_obj(reinterpret_cast<void*>(fn)),
          m_trampoline(fn ? [](void* obj, Args... args) -> R {
              return reinterpret_cast<R (*)(Args...)>(obj)(keel::forward<Args>(args)...);
          }
                          : static_cast<Trampoline>(nullptr)) {}

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_trampoline != nullptr; }

    constexpr R operator()(Args... args) const {
        KEEL_ASSERT(m_trampoline != nullptr);
        return m_trampoline(m_obj, keel::forward<Args>(args)...);
    }

private:
    using Trampoline = R (*)(void*, Args...);
    void* m_obj = nullptr;
    Trampoline m_trampoline = nullptr;
};

static_assert(sizeof(FunctionRef<void()>) == 2 * sizeof(void*), "FunctionRef must be exactly 2 pointers");

} // namespace keel
