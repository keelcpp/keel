#include "keel/math.h"

#if !defined(__GNUC__) && !defined(__clang__)

#include <cmath>

namespace keel {

f32 sqrt(f32 x) noexcept { return std::sqrt(x); }
f64 sqrt(f64 x) noexcept { return std::sqrt(x); }
f32 cbrt(f32 x) noexcept { return std::cbrt(x); }
f64 cbrt(f64 x) noexcept { return std::cbrt(x); }
f32 hypot(f32 x, f32 y) noexcept { return std::hypot(x, y); }
f64 hypot(f64 x, f64 y) noexcept { return std::hypot(x, y); }
f32 pow(f32 b, f32 e) noexcept { return std::pow(b, e); }
f64 pow(f64 b, f64 e) noexcept { return std::pow(b, e); }
f32 fma(f32 a, f32 b, f32 c) noexcept { return std::fma(a, b, c); }
f64 fma(f64 a, f64 b, f64 c) noexcept { return std::fma(a, b, c); }
f32 fmod(f32 x, f32 y) noexcept { return std::fmod(x, y); }
f64 fmod(f64 x, f64 y) noexcept { return std::fmod(x, y); }
f32 remainder(f32 x, f32 y) noexcept { return std::remainder(x, y); }
f64 remainder(f64 x, f64 y) noexcept { return std::remainder(x, y); }

f32 exp(f32 x) noexcept { return std::exp(x); }
f64 exp(f64 x) noexcept { return std::exp(x); }
f32 exp2(f32 x) noexcept { return std::exp2(x); }
f64 exp2(f64 x) noexcept { return std::exp2(x); }
f32 expm1(f32 x) noexcept { return std::expm1(x); }
f64 expm1(f64 x) noexcept { return std::expm1(x); }

f32 log(f32 x) noexcept { return std::log(x); }
f64 log(f64 x) noexcept { return std::log(x); }
f32 log2(f32 x) noexcept { return std::log2(x); }
f64 log2(f64 x) noexcept { return std::log2(x); }
f32 log10(f32 x) noexcept { return std::log10(x); }
f64 log10(f64 x) noexcept { return std::log10(x); }
f32 log1p(f32 x) noexcept { return std::log1p(x); }
f64 log1p(f64 x) noexcept { return std::log1p(x); }

f32 sin(f32 x) noexcept { return std::sin(x); }
f64 sin(f64 x) noexcept { return std::sin(x); }
f32 cos(f32 x) noexcept { return std::cos(x); }
f64 cos(f64 x) noexcept { return std::cos(x); }
f32 tan(f32 x) noexcept { return std::tan(x); }
f64 tan(f64 x) noexcept { return std::tan(x); }

f32 asin(f32 x) noexcept { return std::asin(x); }
f64 asin(f64 x) noexcept { return std::asin(x); }
f32 acos(f32 x) noexcept { return std::acos(x); }
f64 acos(f64 x) noexcept { return std::acos(x); }
f32 atan(f32 x) noexcept { return std::atan(x); }
f64 atan(f64 x) noexcept { return std::atan(x); }
f32 atan2(f32 y, f32 x) noexcept { return std::atan2(y, x); }
f64 atan2(f64 y, f64 x) noexcept { return std::atan2(y, x); }

f32 sinh(f32 x) noexcept { return std::sinh(x); }
f64 sinh(f64 x) noexcept { return std::sinh(x); }
f32 cosh(f32 x) noexcept { return std::cosh(x); }
f64 cosh(f64 x) noexcept { return std::cosh(x); }
f32 tanh(f32 x) noexcept { return std::tanh(x); }
f64 tanh(f64 x) noexcept { return std::tanh(x); }

f32 asinh(f32 x) noexcept { return std::asinh(x); }
f64 asinh(f64 x) noexcept { return std::asinh(x); }
f32 acosh(f32 x) noexcept { return std::acosh(x); }
f64 acosh(f64 x) noexcept { return std::acosh(x); }
f32 atanh(f32 x) noexcept { return std::atanh(x); }
f64 atanh(f64 x) noexcept { return std::atanh(x); }

f32 erf(f32 x) noexcept { return std::erf(x); }
f64 erf(f64 x) noexcept { return std::erf(x); }
f32 erfc(f32 x) noexcept { return std::erfc(x); }
f64 erfc(f64 x) noexcept { return std::erfc(x); }
f32 tgamma(f32 x) noexcept { return std::tgamma(x); }
f64 tgamma(f64 x) noexcept { return std::tgamma(x); }
f32 lgamma(f32 x) noexcept { return std::lgamma(x); }
f64 lgamma(f64 x) noexcept { return std::lgamma(x); }
f32 nextafter(f32 from, f32 to) noexcept { return std::nextafter(from, to); }
f64 nextafter(f64 from, f64 to) noexcept { return std::nextafter(from, to); }
f32 nearbyint(f32 x) noexcept { return std::nearbyint(x); }
f64 nearbyint(f64 x) noexcept { return std::nearbyint(x); }
f32 rint(f32 x) noexcept { return std::rint(x); }
f64 rint(f64 x) noexcept { return std::rint(x); }
f32 fdim(f32 x, f32 y) noexcept { return std::fdim(x, y); }
f64 fdim(f64 x, f64 y) noexcept { return std::fdim(x, y); }

f32 ldexp(f32 x, i32 exp_val) noexcept { return std::ldexp(x, as<int>(exp_val)); }
f64 ldexp(f64 x, i32 exp_val) noexcept { return std::ldexp(x, as<int>(exp_val)); }

f32 frexp(f32 x, i32* exp_out) noexcept {
    int e = 0;
    const f32 m = std::frexp(x, &e);
    if (exp_out) *exp_out = as<i32>(e);
    return m;
}

f64 frexp(f64 x, i32* exp_out) noexcept {
    int e = 0;
    const f64 m = std::frexp(x, &e);
    if (exp_out) *exp_out = as<i32>(e);
    return m;
}

f32 modf(f32 x, f32* iptr) noexcept {
    f32 ip = 0.0f;
    const f32 frac = std::modf(x, &ip);
    if (iptr) *iptr = ip;
    return frac;
}

f64 modf(f64 x, f64* iptr) noexcept {
    f64 ip = 0.0;
    const f64 frac = std::modf(x, &ip);
    if (iptr) *iptr = ip;
    return frac;
}

} // namespace keel

#endif
