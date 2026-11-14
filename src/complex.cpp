#include "keel/complex.h"
#include "keel/math.h"

#include <cmath>

namespace keel {

namespace {

template <typename T> Complex<T> complex_exp(Complex<T> z) noexcept {
    const T r = std::exp(z.re);
    if (z.im == T(0)) return {r, T(0)};
    return {r * std::cos(z.im), r * std::sin(z.im)};
}

template <typename T> Complex<T> complex_log(Complex<T> z) noexcept {
    return {std::log(std::hypot(z.re, z.im)), std::atan2(z.im, z.re)};
}

template <typename T> Complex<T> complex_log10(Complex<T> z) noexcept {
    constexpr T kInvLn10 = T(1) / ln10<T>;
    const Complex<T> w = complex_log(z);
    return {w.re * kInvLn10, w.im * kInvLn10};
}

template <typename T> Complex<T> complex_sqrt(Complex<T> z) noexcept {
    if (std::isinf(z.im)) {
        return {Limits<T>::infinity(), z.im};
    }
    if (z.re == T(0) && z.im == T(0)) {
        return {T(0), z.im};
    }
    const T ax = std::fabs(z.re);
    const T ay = std::fabs(z.im);
    if (std::isnan(ax) || std::isnan(ay)) {
        const T qnan = Limits<T>::quiet_nan();
        return {qnan, qnan};
    }
    if (std::isinf(z.re)) {
        if (z.re > T(0)) {
            return {Limits<T>::infinity(), std::copysign(T(0), z.im)};
        }
        return {T(0), std::copysign(Limits<T>::infinity(), z.im)};
    }

    T w = T(0);
    if (ax >= ay) {
        const T r = ay / ax;
        w = std::sqrt(ax) * std::sqrt(T(0.5) * (T(1) + std::hypot(T(1), r)));
    } else {
        const T r = ax / ay;
        w = std::sqrt(ay) * std::sqrt(T(0.5) * (r + std::hypot(T(1), r)));
    }

    if (z.re >= T(0)) {
        return {w, z.im / (T(2) * w)};
    }
    return {ay / (T(2) * w), std::signbit(z.im) ? -w : w};
}

template <typename T> Complex<T> complex_pow(Complex<T> z, Complex<T> w) noexcept {
    if (z.re == T(0) && z.im == T(0)) {
        if (w.re == T(0) && w.im == T(0)) return {T(1), T(0)};
        if (w.re > T(0) && w.im == T(0)) return {T(0), T(0)};
    }
    if (w.im == T(0) && z.im == T(0) && z.re > T(0)) {
        return {std::pow(z.re, w.re), T(0)};
    }
    return complex_exp(w * complex_log(z));
}

template <typename T> Complex<T> complex_pow_scalar(Complex<T> z, T p) noexcept {
    if (p == T(0)) return {T(1), T(0)};
    if (z.im == T(0) && z.re >= T(0)) {
        return {std::pow(z.re, p), T(0)};
    }
    const T r = std::hypot(z.re, z.im);
    const T theta = std::atan2(z.im, z.re);
    const T rp = std::pow(r, p);
    const T ang = p * theta;
    return {rp * std::cos(ang), rp * std::sin(ang)};
}

template <typename T> Complex<T> scalar_pow_complex(T b, Complex<T> w) noexcept {
    if (b > T(0)) {
        const T r = std::pow(b, w.re);
        const T ang = w.im * std::log(b);
        return {r * std::cos(ang), r * std::sin(ang)};
    }
    return complex_pow(Complex<T>(b, T(0)), w);
}

template <typename T> Complex<T> complex_sin(Complex<T> z) noexcept {
    return {std::sin(z.re) * std::cosh(z.im), std::cos(z.re) * std::sinh(z.im)};
}

template <typename T> Complex<T> complex_cos(Complex<T> z) noexcept {
    return {std::cos(z.re) * std::cosh(z.im), -std::sin(z.re) * std::sinh(z.im)};
}

template <typename T> Complex<T> complex_sinh(Complex<T> z) noexcept {
    return {std::sinh(z.re) * std::cos(z.im), std::cosh(z.re) * std::sin(z.im)};
}

template <typename T> Complex<T> complex_cosh(Complex<T> z) noexcept {
    return {std::cosh(z.re) * std::cos(z.im), std::sinh(z.re) * std::sin(z.im)};
}

template <typename T> Complex<T> complex_tanh(Complex<T> z) noexcept {
    constexpr T kLarge = (sizeof(T) == 4) ? T(12) : T(22);
    if (std::fabs(z.re) > kLarge) {
        return {std::copysign(T(1), z.re), std::copysign(T(0), std::sin(T(2) * z.im))};
    }
    const T d = std::cosh(T(2) * z.re) + std::cos(T(2) * z.im);
    return {std::sinh(T(2) * z.re) / d, std::sin(T(2) * z.im) / d};
}

template <typename T> Complex<T> complex_tan(Complex<T> z) noexcept {
    // tan(z) = -i * tanh(i * z)
    const Complex<T> th = complex_tanh(Complex<T>(-z.im, z.re));
    return {th.im, -th.re};
}

template <typename T> Complex<T> complex_asinh(Complex<T> z) noexcept {
    const bool neg_re = std::signbit(z.re);
    const Complex<T> s = neg_re ? Complex<T>(-z.re, -z.im) : z;
    // Perform intermediate arithmetic in f64 to avoid premature f32 overflow on s * s + 1
    const c64 sd(static_cast<f64>(s.re), static_cast<f64>(s.im));
    const c64 root = complex_sqrt<f64>(sd * sd + 1.0);
    const c64 w = complex_log<f64>(sd + root);
    const Complex<T> out(static_cast<T>(w.re), static_cast<T>(w.im));
    return neg_re ? Complex<T>(-out.re, -out.im) : out;
}

template <typename T> Complex<T> complex_asin(Complex<T> z) noexcept {
    // asin(z) = -i * asinh(i * z)
    const Complex<T> w = complex_asinh(Complex<T>(-z.im, z.re));
    return {w.im, -w.re};
}

template <typename T> Complex<T> complex_acos(Complex<T> z) noexcept {
    // Kahan's formula: acos(z) = 2 * atan2(Re(sqrt(1 - z)), Re(sqrt(1 + z))) + i * asinh(Im(conj(sqrt(1 + z)) * sqrt(1 - z)))
    const c64 zd(static_cast<f64>(z.re), static_cast<f64>(z.im));
    const c64 u = complex_sqrt<f64>(1.0 - zd);
    const c64 v = complex_sqrt<f64>(1.0 + zd);
    const f64 r = 2.0 * std::atan2(u.re, v.re);
    const f64 i = std::asinh(v.re * u.im - v.im * u.re);
    return {static_cast<T>(r), static_cast<T>(i)};
}

template <typename T> Complex<T> complex_acosh(Complex<T> z) noexcept {
    // Kahan's branch-cut-exact formula: 2 * log(sqrt((z + 1)/2) + sqrt((z - 1)/2))
    const c64 zd(static_cast<f64>(z.re), static_cast<f64>(z.im));
    const c64 a = complex_sqrt<f64>((zd + 1.0) * 0.5);
    const c64 b = complex_sqrt<f64>((zd - 1.0) * 0.5);
    const c64 w = 2.0 * complex_log<f64>(a + b);
    return {static_cast<T>(w.re), static_cast<T>(w.im)};
}

template <typename T> Complex<T> complex_atanh(Complex<T> z) noexcept {
    const c64 zd(static_cast<f64>(z.re), static_cast<f64>(z.im));
    const c64 w = 0.5 * (complex_log<f64>(1.0 + zd) - complex_log<f64>(1.0 - zd));
    return {static_cast<T>(w.re), static_cast<T>(w.im)};
}

template <typename T> Complex<T> complex_atan(Complex<T> z) noexcept {
    // atan(z) = -i * atanh(i * z)
    const Complex<T> w = complex_atanh(Complex<T>(-z.im, z.re));
    return {w.im, -w.re};
}

} // namespace

f32 abs(c32 z) noexcept { return std::hypot(z.re, z.im); }
f64 abs(c64 z) noexcept { return std::hypot(z.re, z.im); }

f32 arg(c32 z) noexcept { return std::atan2(z.im, z.re); }
f64 arg(c64 z) noexcept { return std::atan2(z.im, z.re); }

c32 polar(f32 r, f32 theta) noexcept { return {r * std::cos(theta), r * std::sin(theta)}; }
c64 polar(f64 r, f64 theta) noexcept { return {r * std::cos(theta), r * std::sin(theta)}; }

c32 exp(c32 z) noexcept { return complex_exp(z); }
c64 exp(c64 z) noexcept { return complex_exp(z); }
c32 log(c32 z) noexcept { return complex_log(z); }
c64 log(c64 z) noexcept { return complex_log(z); }
c32 log10(c32 z) noexcept { return complex_log10(z); }
c64 log10(c64 z) noexcept { return complex_log10(z); }
c32 sqrt(c32 z) noexcept { return complex_sqrt(z); }
c64 sqrt(c64 z) noexcept { return complex_sqrt(z); }
c32 pow(c32 z, c32 w) noexcept { return complex_pow(z, w); }
c64 pow(c64 z, c64 w) noexcept { return complex_pow(z, w); }
c32 pow(c32 z, f32 p) noexcept { return complex_pow_scalar(z, p); }
c64 pow(c64 z, f64 p) noexcept { return complex_pow_scalar(z, p); }
c32 pow(f32 b, c32 w) noexcept { return scalar_pow_complex(b, w); }
c64 pow(f64 b, c64 w) noexcept { return scalar_pow_complex(b, w); }

c32 sin(c32 z) noexcept { return complex_sin(z); }
c64 sin(c64 z) noexcept { return complex_sin(z); }
c32 cos(c32 z) noexcept { return complex_cos(z); }
c64 cos(c64 z) noexcept { return complex_cos(z); }
c32 tan(c32 z) noexcept { return complex_tan(z); }
c64 tan(c64 z) noexcept { return complex_tan(z); }
c32 sinh(c32 z) noexcept { return complex_sinh(z); }
c64 sinh(c64 z) noexcept { return complex_sinh(z); }
c32 cosh(c32 z) noexcept { return complex_cosh(z); }
c64 cosh(c64 z) noexcept { return complex_cosh(z); }
c32 tanh(c32 z) noexcept { return complex_tanh(z); }
c64 tanh(c64 z) noexcept { return complex_tanh(z); }

c32 asin(c32 z) noexcept { return complex_asin(z); }
c64 asin(c64 z) noexcept { return complex_asin(z); }
c32 acos(c32 z) noexcept { return complex_acos(z); }
c64 acos(c64 z) noexcept { return complex_acos(z); }
c32 atan(c32 z) noexcept { return complex_atan(z); }
c64 atan(c64 z) noexcept { return complex_atan(z); }
c32 asinh(c32 z) noexcept { return complex_asinh(z); }
c64 asinh(c64 z) noexcept { return complex_asinh(z); }
c32 acosh(c32 z) noexcept { return complex_acosh(z); }
c64 acosh(c64 z) noexcept { return complex_acosh(z); }
c32 atanh(c32 z) noexcept { return complex_atanh(z); }
c64 atanh(c64 z) noexcept { return complex_atanh(z); }

} // namespace keel
