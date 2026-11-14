#pragma once

// Opt-in interoperability conversions to C++ standard library types.
// Note: StringRef, ArrayRef<T>, and Complex<T> already convert implicitly to and from std::string_view, std::span,
// and std::complex<T> WITHOUT including this header. Include `keel/interop_std.h` only when converting to
// std::string, std::vector, std::optional, zero-copy std::complex spans/refs, or std::hash.

#include "keel/array.h"
#include "keel/complex.h"
#include "keel/optional.h"
#include "keel/ref.h"
#include "keel/string.h"

#include <complex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace keel {

static_assert(sizeof(std::complex<f32>) == sizeof(c32) && alignof(std::complex<f32>) == alignof(c32));
static_assert(sizeof(std::complex<f64>) == sizeof(c64) && alignof(std::complex<f64>) == alignof(c64));

[[nodiscard]] inline std::string to_std_string(StringRef s) {
    return std::string(s.data(), s.size());
}

[[nodiscard]] constexpr std::string_view to_std_string_view(StringRef s) noexcept {
    return std::string_view(s.data(), s.size());
}

template <typename T> [[nodiscard]] constexpr std::span<const T> to_std_span(ArrayRef<T> arr) noexcept {
    return std::span<const T>(arr.data(), arr.size());
}

template <typename T> [[nodiscard]] constexpr std::span<T> to_std_span(MutArrayRef<T> arr) noexcept {
    return std::span<T>(arr.data(), arr.size());
}

template <typename T> [[nodiscard]] std::vector<T> to_std_vector(ArrayRef<T> arr) {
    return std::vector<T>(arr.begin(), arr.end());
}

template <typename T> [[nodiscard]] constexpr std::optional<T> to_std_optional(const Optional<T>& opt) {
    if (opt.has_value()) return std::optional<T>(*opt);
    return std::nullopt;
}

template <typename T> [[nodiscard]] constexpr Optional<T> from_std_optional(const std::optional<T>& opt) {
    if (opt.has_value()) return Optional<T>(*opt);
    return kNullOpt;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr std::complex<T> to_std_complex(Complex<T> z) noexcept {
    return std::complex<T>(z.re, z.im);
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr Complex<T> from_std_complex(std::complex<T> z) noexcept {
    return Complex<T>(z.real(), z.imag());
}

// ISO C++ ([complex.numbers.general]) guarantees `std::complex<T>` has exact array layout `T[2]`.
template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline ArrayRef<Complex<T>> as_complex_ref(const std::complex<T>* data, usize size) noexcept {
    return ArrayRef<Complex<T>>(reinterpret_cast<const Complex<T>*>(data), size);
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline ArrayRef<Complex<T>> as_complex_ref(std::span<const std::complex<T>> s) noexcept {
    return ArrayRef<Complex<T>>(reinterpret_cast<const Complex<T>*>(s.data()), s.size());
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline MutArrayRef<Complex<T>> as_mut_complex_ref(std::complex<T>* data, usize size) noexcept {
    return MutArrayRef<Complex<T>>(reinterpret_cast<Complex<T>*>(data), size);
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline MutArrayRef<Complex<T>> as_mut_complex_ref(std::span<std::complex<T>> s) noexcept {
    return MutArrayRef<Complex<T>>(reinterpret_cast<Complex<T>*>(s.data()), s.size());
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline std::span<const std::complex<T>> to_std_complex_span(ArrayRef<Complex<T>> s) noexcept {
    return std::span<const std::complex<T>>(reinterpret_cast<const std::complex<T>*>(s.data()), s.size());
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline std::span<std::complex<T>> to_std_complex_span(MutArrayRef<Complex<T>> s) noexcept {
    return std::span<std::complex<T>>(reinterpret_cast<std::complex<T>*>(s.data()), s.size());
}

} // namespace keel

template <typename T>
    requires keel::IsFloatingPoint<T>
struct std::hash<keel::Complex<T>> {
    std::size_t operator()(keel::Complex<T> z) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::Complex<T>>{}(z));
    }
};

template <> struct std::hash<keel::StringRef> {
    std::size_t operator()(keel::StringRef s) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::StringRef>{}(s));
    }
};

template <> struct std::hash<keel::String> {
    std::size_t operator()(const keel::String& s) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::String>{}(s));
    }
};

template <keel::usize N> struct std::hash<keel::SmallString<N>> {
    std::size_t operator()(const keel::SmallString<N>& s) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::SmallString<N>>{}(s));
    }
};

template <keel::usize N> struct std::hash<keel::FixedString<N>> {
    std::size_t operator()(const keel::FixedString<N>& s) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::FixedString<N>>{}(s));
    }
};
