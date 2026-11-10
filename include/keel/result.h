#pragma once

#include "keel/error.h"

namespace keel {

template <typename T, typename E = Error> class [[nodiscard]] Result {
public:
    constexpr Result(const T& val)
        requires IsConstructible<T, const T&>
        : m_value(val), m_has_value(true) {}
    constexpr Result(T&& val) noexcept : m_value(keel::move(val)), m_has_value(true) {}

    constexpr Result(const E& err) noexcept : m_error(err), m_has_value(false) { KEEL_ASSERT(!err.is_ok()); }
    constexpr Result(E&& err) noexcept : m_error(keel::move(err)), m_has_value(false) { KEEL_ASSERT(!m_error.is_ok()); }

    constexpr Result(ErrorCode code) noexcept
        requires IsSame<E, Error>
        : m_error(Error{code, 0}), m_has_value(false) {
        KEEL_ASSERT(code != ErrorCode::Ok);
    }

    constexpr ~Result() noexcept
        requires(IsTriviallyDestructible<T> && IsTriviallyDestructible<E>)
    = default;

    constexpr ~Result() noexcept
        requires(!(IsTriviallyDestructible<T> && IsTriviallyDestructible<E>))
    {
        destroy();
    }

    constexpr Result(const Result&) = default;
    constexpr Result(const Result& other)
        requires(!(IsTriviallyCopyable<T> && IsTriviallyCopyable<E>) &&
                 IsConstructible<T, const T&> && IsConstructible<E, const E&>)
        : m_has_value(other.m_has_value) {
        if (m_has_value) {
            ::new (&m_value) T(other.m_value);
        } else {
            ::new (&m_error) E(other.m_error);
        }
    }
    Result(const Result&)
        requires(!(IsConstructible<T, const T&> && IsConstructible<E, const E&>))
    = delete;

    constexpr Result(Result&&) noexcept = default;
    constexpr Result(Result&& other) noexcept
        requires(!(IsTriviallyCopyable<T> && IsTriviallyCopyable<E>))
        : m_has_value(other.m_has_value) {
        if (m_has_value) {
            ::new (&m_value) T(keel::move(other.m_value));
        } else {
            ::new (&m_error) E(keel::move(other.m_error));
        }
    }

    constexpr Result& operator=(const Result&) = default;
    constexpr Result& operator=(const Result& other)
        requires(!(IsTriviallyCopyable<T> && IsTriviallyCopyable<E>) &&
                 IsConstructible<T, const T&> && IsConstructible<E, const E&>)
    {
        if (this != &other) {
            destroy();
            m_has_value = other.m_has_value;
            if (m_has_value) {
                ::new (&m_value) T(other.m_value);
            } else {
                ::new (&m_error) E(other.m_error);
            }
        }
        return *this;
    }
    Result& operator=(const Result&)
        requires(!(IsConstructible<T, const T&> && IsConstructible<E, const E&>))
    = delete;

    constexpr Result& operator=(Result&&) noexcept = default;
    constexpr Result& operator=(Result&& other) noexcept
        requires(!(IsTriviallyCopyable<T> && IsTriviallyCopyable<E>))
    {
        if (this != &other) {
            destroy();
            m_has_value = other.m_has_value;
            if (m_has_value) {
                ::new (&m_value) T(keel::move(other.m_value));
            } else {
                ::new (&m_error) E(keel::move(other.m_error));
            }
        }
        return *this;
    }

    [[nodiscard]] constexpr bool is_ok() const noexcept { return m_has_value; }
    [[nodiscard]] constexpr bool is_err() const noexcept { return !m_has_value; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_has_value; }

    [[nodiscard]] constexpr T& value() & noexcept {
        KEEL_ASSERT(m_has_value);
        return m_value;
    }

    [[nodiscard]] constexpr const T& value() const& noexcept {
        KEEL_ASSERT(m_has_value);
        return m_value;
    }

    [[nodiscard]] constexpr T&& value() && noexcept {
        KEEL_ASSERT(m_has_value);
        return keel::move(m_value);
    }

    [[nodiscard]] constexpr T& operator*() & noexcept { return value(); }
    [[nodiscard]] constexpr const T& operator*() const& noexcept { return value(); }
    [[nodiscard]] constexpr T&& operator*() && noexcept { return keel::move(value()); }

    [[nodiscard]] constexpr T* operator->() noexcept {
        KEEL_ASSERT(m_has_value);
        return &m_value;
    }

    [[nodiscard]] constexpr const T* operator->() const noexcept {
        KEEL_ASSERT(m_has_value);
        return &m_value;
    }

    [[nodiscard]] constexpr const E& error() const noexcept {
        KEEL_ASSERT(!m_has_value);
        return m_error;
    }

    template <typename U> [[nodiscard]] constexpr T value_or(U&& fallback) const& {
        return m_has_value ? m_value : static_cast<T>(keel::forward<U>(fallback));
    }

    template <typename U> [[nodiscard]] constexpr T value_or(U&& fallback) && {
        return m_has_value ? keel::move(m_value) : static_cast<T>(keel::forward<U>(fallback));
    }

private:
    constexpr void destroy() noexcept {
        if (m_has_value) {
            m_value.~T();
        } else {
            m_error.~E();
        }
    }

    union {
        T m_value;
        E m_error;
    };
    bool m_has_value = false;
};

template <typename E> class [[nodiscard]] Result<void, E> {
public:
    constexpr Result() noexcept : m_error{}, m_has_value(true) {}
    constexpr Result(const E& err) noexcept : m_error(err), m_has_value(err.is_ok()) {}
    constexpr Result(ErrorCode code) noexcept
        requires IsSame<E, Error>
        : m_error(Error{code, 0}), m_has_value(code == ErrorCode::Ok) {}

    [[nodiscard]] constexpr bool is_ok() const noexcept { return m_has_value; }
    [[nodiscard]] constexpr bool is_err() const noexcept { return !m_has_value; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_has_value; }

    [[nodiscard]] constexpr const E& error() const noexcept {
        KEEL_ASSERT(!m_has_value);
        return m_error;
    }

private:
    E m_error{};
    bool m_has_value = true;
};

#define KEEL_CONCAT_INNER_(a, b) a##b
#define KEEL_CONCAT_(a, b) KEEL_CONCAT_INNER_(a, b)

#define KEEL_TRY(expr)                                                                                                 \
    do {                                                                                                               \
        auto KEEL_CONCAT_(_keel_try_, __LINE__) = (expr);                                                              \
        if (!KEEL_CONCAT_(_keel_try_, __LINE__).is_ok()) [[unlikely]] {                                                \
            return KEEL_CONCAT_(_keel_try_, __LINE__).error();                                                         \
        }                                                                                                              \
    } while (0)

#define KEEL_TRY_ASSIGN(lhs, expr)                                                                                     \
    auto KEEL_CONCAT_(_keel_res_, __LINE__) = (expr);                                                                  \
    if (!KEEL_CONCAT_(_keel_res_, __LINE__).is_ok()) [[unlikely]] {                                                    \
        return KEEL_CONCAT_(_keel_res_, __LINE__).error();                                                             \
    }                                                                                                                  \
    lhs = ::keel::move(KEEL_CONCAT_(_keel_res_, __LINE__).value())

} // namespace keel
