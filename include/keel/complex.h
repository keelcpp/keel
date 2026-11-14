#pragma once

#include "keel/core.h"
#include "keel/hash.h"

namespace keel {

// Flat, standard-layout IEEE-754 complex number (`T[2]` memory layout: `re` at offset 0, `im` at offset `sizeof(T)`).
// Uses direct algebraic `+`, `-`, `*` and Smith's overflow-safe `/` without Annex G library calls, enabling
// constexpr evaluation, fast Debug builds, and SIMD auto-vectorization.
template <typename T>
    requires IsFloatingPoint<T>
struct Complex {
    using ValueType = T;

    T re = T(0);
    T im = T(0);

    constexpr Complex() noexcept = default;
    constexpr Complex(T r, T i = T(0)) noexcept : re(r), im(i) {}

    template <typename U>
        requires IsFloatingPoint<U> && (!IsSame<U, T>)
    explicit(sizeof(U) > sizeof(T)) constexpr Complex(const Complex<U>& other) noexcept
        : re(static_cast<T>(other.re)), im(static_cast<T>(other.im)) {}

    // Zero-header conversion from std::complex<T>
    template <typename C>
        requires(!IsSame<RemoveCvRef<C>, Complex>) && IsTriviallyCopyable<C> && (sizeof(C) == 2 * sizeof(T)) &&
                requires(const C& z) {
                    typename C::value_type;
                    requires IsSame<RemoveCv<typename C::value_type>, T>;
                    requires IsSame<RemoveCvRef<decltype(z.real())>, T>;
                    requires IsSame<RemoveCvRef<decltype(z.imag())>, T>;
                }
    constexpr Complex(const C& z) noexcept : re(z.real()), im(z.imag()) {}

    // Zero-header conversion to std::complex<T>
    template <typename C>
        requires(!IsSame<RemoveCvRef<C>, Complex>) && IsTriviallyCopyable<C> && (sizeof(C) == 2 * sizeof(T)) &&
                requires(T r, T i) {
                    typename C::value_type;
                    requires IsSame<RemoveCv<typename C::value_type>, T>;
                    C(r, i);
                }
    [[nodiscard]] constexpr operator C() const noexcept {
        return C(re, im);
    }

    [[nodiscard]] constexpr T real() const noexcept { return re; }
    [[nodiscard]] constexpr T imag() const noexcept { return im; }
    constexpr void real(T v) noexcept { re = v; }
    constexpr void imag(T v) noexcept { im = v; }

    [[nodiscard]] constexpr Complex operator+() const noexcept { return *this; }
    [[nodiscard]] constexpr Complex operator-() const noexcept { return {-re, -im}; }

    constexpr Complex& operator+=(Complex rhs) noexcept {
        re += rhs.re;
        im += rhs.im;
        return *this;
    }

    constexpr Complex& operator+=(T s) noexcept {
        re += s;
        return *this;
    }

    constexpr Complex& operator-=(Complex rhs) noexcept {
        re -= rhs.re;
        im -= rhs.im;
        return *this;
    }

    constexpr Complex& operator-=(T s) noexcept {
        re -= s;
        return *this;
    }

    constexpr Complex& operator*=(Complex rhs) noexcept {
        const T next_re = re * rhs.re - im * rhs.im;
        const T next_im = re * rhs.im + im * rhs.re;
        re = next_re;
        im = next_im;
        return *this;
    }

    constexpr Complex& operator*=(T s) noexcept {
        re *= s;
        im *= s;
        return *this;
    }

    constexpr Complex& operator/=(Complex rhs) noexcept {
        const T ar = (rhs.re < T(0)) ? -rhs.re : rhs.re;
        const T ai = (rhs.im < T(0)) ? -rhs.im : rhs.im;
        if (ar >= ai) {
            const T ratio = rhs.im / rhs.re;
            const T denom = rhs.re + rhs.im * ratio;
            const T next_re = (re + im * ratio) / denom;
            const T next_im = (im - re * ratio) / denom;
            re = next_re;
            im = next_im;
        } else {
            const T ratio = rhs.re / rhs.im;
            const T denom = rhs.im + rhs.re * ratio;
            const T next_re = (re * ratio + im) / denom;
            const T next_im = (im * ratio - re) / denom;
            re = next_re;
            im = next_im;
        }
        return *this;
    }

    constexpr Complex& operator/=(T s) noexcept {
        const T inv = T(1) / s;
        re *= inv;
        im *= inv;
        return *this;
    }

    [[nodiscard]] friend constexpr Complex operator+(Complex a, Complex b) noexcept {
        return {a.re + b.re, a.im + b.im};
    }
    [[nodiscard]] friend constexpr Complex operator+(Complex a, T s) noexcept { return {a.re + s, a.im}; }
    [[nodiscard]] friend constexpr Complex operator+(T s, Complex a) noexcept { return {s + a.re, a.im}; }

    [[nodiscard]] friend constexpr Complex operator-(Complex a, Complex b) noexcept {
        return {a.re - b.re, a.im - b.im};
    }
    [[nodiscard]] friend constexpr Complex operator-(Complex a, T s) noexcept { return {a.re - s, a.im}; }
    [[nodiscard]] friend constexpr Complex operator-(T s, Complex a) noexcept { return {s - a.re, -a.im}; }

    [[nodiscard]] friend constexpr Complex operator*(Complex a, Complex b) noexcept {
        return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
    }
    [[nodiscard]] friend constexpr Complex operator*(Complex a, T s) noexcept { return {a.re * s, a.im * s}; }
    [[nodiscard]] friend constexpr Complex operator*(T s, Complex a) noexcept { return {s * a.re, s * a.im}; }

    [[nodiscard]] friend constexpr Complex operator/(Complex a, Complex b) noexcept {
        a /= b;
        return a;
    }
    [[nodiscard]] friend constexpr Complex operator/(Complex a, T s) noexcept {
        const T inv = T(1) / s;
        return {a.re * inv, a.im * inv};
    }
    [[nodiscard]] friend constexpr Complex operator/(T s, Complex b) noexcept {
        Complex res(s, T(0));
        res /= b;
        return res;
    }

    [[nodiscard]] friend constexpr bool operator==(Complex a, Complex b) noexcept {
        return a.re == b.re && a.im == b.im;
    }
    [[nodiscard]] friend constexpr bool operator==(Complex a, T s) noexcept { return a.re == s && a.im == T(0); }
    [[nodiscard]] friend constexpr bool operator==(T s, Complex a) noexcept { return s == a.re && T(0) == a.im; }

    [[nodiscard]] friend constexpr bool operator!=(Complex a, Complex b) noexcept { return !(a == b); }
    [[nodiscard]] friend constexpr bool operator!=(Complex a, T s) noexcept { return !(a == s); }
    [[nodiscard]] friend constexpr bool operator!=(T s, Complex a) noexcept { return !(s == a); }
};

// Component-precision aliases matching f32 and f64 (`sizeof(c32) == 8`, `sizeof(c64) == 16`).
using c32 = Complex<f32>;
using c64 = Complex<f64>;

static_assert(sizeof(c32) == 2 * sizeof(f32) && alignof(c32) == alignof(f32));
static_assert(sizeof(c64) == 2 * sizeof(f64) && alignof(c64) == alignof(f64));
static_assert(IsTriviallyCopyable<c32> && IsTriviallyCopyable<c64>);

template <typename T> [[nodiscard]] constexpr T real(Complex<T> z) noexcept { return z.re; }
template <typename T> [[nodiscard]] constexpr T imag(Complex<T> z) noexcept { return z.im; }
template <typename T> [[nodiscard]] constexpr Complex<T> conj(Complex<T> z) noexcept { return {z.re, -z.im}; }
template <typename T> [[nodiscard]] constexpr T norm_sq(Complex<T> z) noexcept { return z.re * z.re + z.im * z.im; }

[[nodiscard]] f32 abs(c32 z) noexcept;
[[nodiscard]] f64 abs(c64 z) noexcept;
[[nodiscard]] f32 arg(c32 z) noexcept;
[[nodiscard]] f64 arg(c64 z) noexcept;
[[nodiscard]] c32 polar(f32 r, f32 theta = 0.0f) noexcept;
[[nodiscard]] c64 polar(f64 r, f64 theta = 0.0) noexcept;

template <> struct Hash<c32> {
    [[nodiscard]] constexpr u64 operator()(c32 z) const noexcept {
        const f32 r = (z.re == 0.0f) ? 0.0f : z.re;
        const f32 i = (z.im == 0.0f) ? 0.0f : z.im;
        const u32 rb = __builtin_bit_cast(u32, r);
        const u32 ib = __builtin_bit_cast(u32, i);
        return hash_u64((static_cast<u64>(ib) << 32) | static_cast<u64>(rb));
    }
};

template <> struct Hash<c64> {
    [[nodiscard]] constexpr u64 operator()(c64 z) const noexcept {
        const f64 r = (z.re == 0.0) ? 0.0 : z.re;
        const f64 i = (z.im == 0.0) ? 0.0 : z.im;
        const u64 rb = __builtin_bit_cast(u64, r);
        const u64 ib = __builtin_bit_cast(u64, i);
        return hash_combine(hash_u64(rb), ib);
    }
};

} // namespace keel
