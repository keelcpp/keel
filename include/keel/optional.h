#pragma once

#include "keel/core.h"

namespace keel {

struct NullOptT {
    explicit constexpr NullOptT(int) noexcept {}
};

inline constexpr NullOptT kNullOpt{0};

template <typename T> class Optional {
public:
    constexpr Optional() noexcept : m_dummy(0), m_has_value(false) {}
    constexpr Optional(NullOptT) noexcept : m_dummy(0), m_has_value(false) {}

    constexpr Optional(const T& val) : m_value(val), m_has_value(true) {}
    constexpr Optional(T&& val) noexcept : m_value(keel::move(val)), m_has_value(true) {}

    constexpr ~Optional() noexcept
        requires IsTriviallyDestructible<T>
    = default;

    constexpr ~Optional() noexcept
        requires(!IsTriviallyDestructible<T>)
    {
        reset();
    }

    constexpr Optional(const Optional&) = default;
    constexpr Optional(const Optional& other)
        requires(!IsTriviallyCopyable<T>)
        : m_dummy(0), m_has_value(other.m_has_value) {
        if (m_has_value) {
            ::new (&m_value) T(other.m_value);
        }
    }

    constexpr Optional(Optional&&) noexcept = default;
    constexpr Optional(Optional&& other) noexcept
        requires(!IsTriviallyCopyable<T>)
        : m_dummy(0), m_has_value(other.m_has_value) {
        if (m_has_value) {
            ::new (&m_value) T(keel::move(other.m_value));
            other.reset();
        }
    }

    constexpr Optional& operator=(const Optional&) = default;
    constexpr Optional& operator=(const Optional& other)
        requires(!IsTriviallyCopyable<T>)
    {
        if (this != &other) {
            if (m_has_value && other.m_has_value) {
                m_value = other.m_value;
            } else if (other.m_has_value) {
                emplace(other.m_value);
            } else {
                reset();
            }
        }
        return *this;
    }

    constexpr Optional& operator=(Optional&&) noexcept = default;
    constexpr Optional& operator=(Optional&& other) noexcept
        requires(!IsTriviallyCopyable<T>)
    {
        if (this != &other) {
            if (m_has_value && other.m_has_value) {
                m_value = keel::move(other.m_value);
                other.reset();
            } else if (other.m_has_value) {
                emplace(keel::move(other.m_value));
                other.reset();
            } else {
                reset();
            }
        }
        return *this;
    }

    constexpr Optional& operator=(NullOptT) noexcept {
        reset();
        return *this;
    }

    template <typename... Args> constexpr T& emplace(Args&&... args) {
        reset();
        ::new (&m_value) T(keel::forward<Args>(args)...);
        m_has_value = true;
        return m_value;
    }

    constexpr void reset() noexcept {
        if (m_has_value) {
            if constexpr (!IsTriviallyDestructible<T>) {
                m_value.~T();
            }
            m_has_value = false;
        }
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return m_has_value; }
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

    template <typename U> [[nodiscard]] constexpr T value_or(U&& fallback) const& {
        return m_has_value ? m_value : static_cast<T>(keel::forward<U>(fallback));
    }

    template <typename U> [[nodiscard]] constexpr T value_or(U&& fallback) && {
        return m_has_value ? keel::move(m_value) : static_cast<T>(keel::forward<U>(fallback));
    }

    [[nodiscard]] friend constexpr bool operator==(const Optional& a, const Optional& b) noexcept {
        if (a.m_has_value != b.m_has_value) return false;
        return !a.m_has_value || (a.m_value == b.m_value);
    }

    [[nodiscard]] friend constexpr bool operator==(const Optional& a, NullOptT) noexcept { return !a.m_has_value; }
    [[nodiscard]] friend constexpr bool operator==(const Optional& a, const T& val) noexcept {
        return a.m_has_value && (a.m_value == val);
    }

private:
    union {
        char m_dummy;
        T m_value;
    };
    bool m_has_value = false;
};

// Zero-overhead optional storing a reserved sentinel value inside T itself (sizeof == sizeof(T)).
template <typename T, T Sentinel> class SentinelOptional {
public:
    constexpr SentinelOptional() noexcept : m_value(Sentinel) {}
    constexpr SentinelOptional(NullOptT) noexcept : m_value(Sentinel) {}
    constexpr SentinelOptional(T val) noexcept : m_value(val) { KEEL_ASSERT(!(val == Sentinel)); }

    constexpr void reset() noexcept { m_value = Sentinel; }

    [[nodiscard]] constexpr bool has_value() const noexcept { return !(m_value == Sentinel); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return !(m_value == Sentinel); }

    [[nodiscard]] constexpr const T& value() const noexcept {
        KEEL_ASSERT(has_value());
        return m_value;
    }

    [[nodiscard]] constexpr const T& operator*() const noexcept { return value(); }
    [[nodiscard]] constexpr const T* operator->() const noexcept {
        KEEL_ASSERT(has_value());
        return &m_value;
    }

    [[nodiscard]] constexpr T value_or(T fallback) const noexcept { return has_value() ? m_value : fallback; }

    [[nodiscard]] friend constexpr bool operator==(SentinelOptional a, SentinelOptional b) noexcept {
        return a.m_value == b.m_value;
    }

    [[nodiscard]] friend constexpr bool operator==(SentinelOptional a, NullOptT) noexcept { return !a.has_value(); }
    [[nodiscard]] friend constexpr bool operator==(SentinelOptional a, T val) noexcept {
        return a.has_value() && a.m_value == val;
    }

private:
    T m_value = Sentinel;
};

} // namespace keel
