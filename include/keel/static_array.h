#pragma once

#include "keel/ref.h"

namespace keel {

// Trivially copyable, constexpr aggregate fixed-size array (replacement for std::array<T, N>).
template <typename T, usize N> struct StaticArray {
    T m_data[N];

    [[nodiscard]] constexpr T* data() noexcept { return m_data; }
    [[nodiscard]] constexpr const T* data() const noexcept { return m_data; }

    [[nodiscard]] static constexpr usize size() noexcept { return N; }
    [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    [[nodiscard]] constexpr T* begin() noexcept { return m_data; }
    [[nodiscard]] constexpr const T* begin() const noexcept { return m_data; }
    [[nodiscard]] constexpr const T* cbegin() const noexcept { return m_data; }

    [[nodiscard]] constexpr T* end() noexcept { return m_data + N; }
    [[nodiscard]] constexpr const T* end() const noexcept { return m_data + N; }
    [[nodiscard]] constexpr const T* cend() const noexcept { return m_data + N; }

    [[nodiscard]] constexpr T& operator[](usize i) noexcept {
        KEEL_BOUNDS_ASSERT(i < N);
        return m_data[i];
    }

    [[nodiscard]] constexpr const T& operator[](usize i) const noexcept {
        KEEL_BOUNDS_ASSERT(i < N);
        return m_data[i];
    }

    [[nodiscard]] constexpr T& front() noexcept { return m_data[0]; }
    [[nodiscard]] constexpr const T& front() const noexcept { return m_data[0]; }

    [[nodiscard]] constexpr T& back() noexcept { return m_data[N - 1]; }
    [[nodiscard]] constexpr const T& back() const noexcept { return m_data[N - 1]; }

    constexpr void fill(const T& val) noexcept {
        for (usize i = 0; i < N; ++i) {
            m_data[i] = val;
        }
    }

    [[nodiscard]] constexpr operator ArrayRef<T>() const noexcept { return ArrayRef<T>(m_data, N); }
    [[nodiscard]] constexpr operator MutArrayRef<T>() noexcept { return MutArrayRef<T>(m_data, N); }
    [[nodiscard]] constexpr ArrayRef<T> as_ref() const noexcept { return ArrayRef<T>(m_data, N); }
    [[nodiscard]] constexpr MutArrayRef<T> as_mut_ref() noexcept { return MutArrayRef<T>(m_data, N); }

    [[nodiscard]] constexpr ArrayRef<T> slice(usize offset, usize count = kInvalidIndex) const noexcept {
        return as_ref().slice(offset, count);
    }

    [[nodiscard]] constexpr bool operator==(const StaticArray& other) const noexcept = default;
};

template <typename T> struct StaticArray<T, 0> {
    [[nodiscard]] constexpr T* data() noexcept { return nullptr; }
    [[nodiscard]] constexpr const T* data() const noexcept { return nullptr; }

    [[nodiscard]] static constexpr usize size() noexcept { return 0; }
    [[nodiscard]] static constexpr bool empty() noexcept { return true; }

    [[nodiscard]] constexpr T* begin() noexcept { return nullptr; }
    [[nodiscard]] constexpr const T* begin() const noexcept { return nullptr; }
    [[nodiscard]] constexpr const T* cbegin() const noexcept { return nullptr; }

    [[nodiscard]] constexpr T* end() noexcept { return nullptr; }
    [[nodiscard]] constexpr const T* end() const noexcept { return nullptr; }
    [[nodiscard]] constexpr const T* cend() const noexcept { return nullptr; }

    [[nodiscard]] constexpr T& operator[](usize) noexcept {
        KEEL_BOUNDS_ASSERT(false);
        return *static_cast<T*>(nullptr);
    }

    [[nodiscard]] constexpr const T& operator[](usize) const noexcept {
        KEEL_BOUNDS_ASSERT(false);
        return *static_cast<const T*>(nullptr);
    }

    constexpr void fill(const T&) noexcept {}

    [[nodiscard]] constexpr operator ArrayRef<T>() const noexcept { return ArrayRef<T>(); }
    [[nodiscard]] constexpr operator MutArrayRef<T>() noexcept { return MutArrayRef<T>(); }
    [[nodiscard]] constexpr ArrayRef<T> as_ref() const noexcept { return ArrayRef<T>(); }
    [[nodiscard]] constexpr MutArrayRef<T> as_mut_ref() noexcept { return MutArrayRef<T>(); }

    [[nodiscard]] constexpr bool operator==(const StaticArray&) const noexcept = default;
};

template <typename T, typename... U>
    requires(IsSame<T, U> && ...)
StaticArray(T, U...) -> StaticArray<T, 1 + sizeof...(U)>;

} // namespace keel
