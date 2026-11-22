#pragma once

#include "keel/core.h"

namespace keel {

// ============================================================================
// 1. Mathematical Constants & Angle Conversions (replaces <numbers>)
// ============================================================================

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T pi = static_cast<T>(3.141592653589793238462643383279502884L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T tau = static_cast<T>(6.283185307179586476925286766559005768L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T half_pi = static_cast<T>(1.570796326794896619231321691639751442L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T inv_pi = static_cast<T>(0.318309886183790671537767526745028724L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T sqrt2 = static_cast<T>(1.414213562373095048801688724209698079L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T inv_sqrt2 = static_cast<T>(0.707106781186547524400844362104849039L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T euler_e = static_cast<T>(2.718281828459045235360287471352662498L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T ln2 = static_cast<T>(0.693147180559945309417232121458176568L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T ln10 = static_cast<T>(2.302585092994045684017991454684364208L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T sqrt_pi = static_cast<T>(1.772453850905516027298167483341145183L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T inv_sqrt_pi = static_cast<T>(0.564189583547756286948079451560772586L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T sqrt_2pi = static_cast<T>(2.506628274631000502415765284811045253L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T inv_sqrt_2pi = static_cast<T>(0.398942280401432677939946059934381868L);

template <typename T>
    requires IsFloatingPoint<T>
inline constexpr T euler_gamma = static_cast<T>(0.577215664901532860606512090082402431L);

inline constexpr f64 kPi = pi<f64>;
inline constexpr f32 kPiF = pi<f32>;
inline constexpr f64 kTau = tau<f64>;
inline constexpr f32 kTauF = tau<f32>;
inline constexpr f64 kHalfPi = half_pi<f64>;
inline constexpr f32 kHalfPiF = half_pi<f32>;
inline constexpr f64 kInvPi = inv_pi<f64>;
inline constexpr f32 kInvPiF = inv_pi<f32>;
inline constexpr f64 kSqrt2 = sqrt2<f64>;
inline constexpr f32 kSqrt2F = sqrt2<f32>;
inline constexpr f64 kInvSqrt2 = inv_sqrt2<f64>;
inline constexpr f32 kInvSqrt2F = inv_sqrt2<f32>;
inline constexpr f64 kE = euler_e<f64>;
inline constexpr f32 kEF = euler_e<f32>;
inline constexpr f64 kLn2 = ln2<f64>;
inline constexpr f32 kLn2F = ln2<f32>;
inline constexpr f64 kLn10 = ln10<f64>;
inline constexpr f32 kLn10F = ln10<f32>;
inline constexpr f64 kSqrtPi = sqrt_pi<f64>;
inline constexpr f32 kSqrtPiF = sqrt_pi<f32>;
inline constexpr f64 kInvSqrtPi = inv_sqrt_pi<f64>;
inline constexpr f32 kInvSqrtPiF = inv_sqrt_pi<f32>;
inline constexpr f64 kSqrt2Pi = sqrt_2pi<f64>;
inline constexpr f32 kSqrt2PiF = sqrt_2pi<f32>;
inline constexpr f64 kInvSqrt2Pi = inv_sqrt_2pi<f64>;
inline constexpr f32 kInvSqrt2PiF = inv_sqrt_2pi<f32>;
inline constexpr f64 kEulerGamma = euler_gamma<f64>;
inline constexpr f32 kEulerGammaF = euler_gamma<f32>;

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr T deg_to_rad(T deg) noexcept {
    return deg * (pi<T> / static_cast<T>(180));
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr T rad_to_deg(T rad) noexcept {
    return rad * (static_cast<T>(180) / pi<T>);
}

// ============================================================================
// 2. IEEE-754 Special Value Shorthands (Limits<T> lives in "keel/core.h")
// ============================================================================

inline constexpr f64 kNaN = Limits<f64>::quiet_nan();
inline constexpr f32 kNaNF = Limits<f32>::quiet_nan();
inline constexpr f64 kInf = Limits<f64>::infinity();
inline constexpr f32 kInfF = Limits<f32>::infinity();

// ============================================================================
// 3. Sign, Magnitude & IEEE-754 Classification
// ============================================================================

[[nodiscard]] constexpr bool is_nan(f32 x) noexcept {
    return (__builtin_bit_cast(u32, x) & 0x7FFFFFFFu) > 0x7F800000u;
}

[[nodiscard]] constexpr bool is_nan(f64 x) noexcept {
    return (__builtin_bit_cast(u64, x) & 0x7FFFFFFFFFFFFFFFULL) > 0x7FF0000000000000ULL;
}

[[nodiscard]] constexpr bool is_inf(f32 x) noexcept {
    return (__builtin_bit_cast(u32, x) & 0x7FFFFFFFu) == 0x7F800000u;
}

[[nodiscard]] constexpr bool is_inf(f64 x) noexcept {
    return (__builtin_bit_cast(u64, x) & 0x7FFFFFFFFFFFFFFFULL) == 0x7FF0000000000000ULL;
}

[[nodiscard]] constexpr bool is_finite(f32 x) noexcept {
    return (__builtin_bit_cast(u32, x) & 0x7F800000u) != 0x7F800000u;
}

[[nodiscard]] constexpr bool is_finite(f64 x) noexcept {
    return (__builtin_bit_cast(u64, x) & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}

template <typename T>
    requires IsIntegral<T>
[[nodiscard]] constexpr bool is_nan(T) noexcept {
    return false;
}

template <typename T>
    requires IsIntegral<T>
[[nodiscard]] constexpr bool is_inf(T) noexcept {
    return false;
}

template <typename T>
    requires IsIntegral<T>
[[nodiscard]] constexpr bool is_finite(T) noexcept {
    return true;
}

[[nodiscard]] constexpr bool signbit(f32 x) noexcept {
    return (__builtin_bit_cast(u32, x) >> 31) != 0u;
}

[[nodiscard]] constexpr bool signbit(f64 x) noexcept {
    return (__builtin_bit_cast(u64, x) >> 63) != 0ULL;
}

[[nodiscard]] constexpr f32 copysign(f32 mag, f32 sgn) noexcept {
    const u32 m = __builtin_bit_cast(u32, mag) & 0x7FFFFFFFu;
    const u32 s = __builtin_bit_cast(u32, sgn) & 0x80000000u;
    return __builtin_bit_cast(f32, m | s);
}

[[nodiscard]] constexpr f64 copysign(f64 mag, f64 sgn) noexcept {
    const u64 m = __builtin_bit_cast(u64, mag) & 0x7FFFFFFFFFFFFFFFULL;
    const u64 s = __builtin_bit_cast(u64, sgn) & 0x8000000000000000ULL;
    return __builtin_bit_cast(f64, m | s);
}

template <typename T>
    requires(IsSigned<T> && IsIntegral<T>)
[[nodiscard]] constexpr T abs(T x) noexcept {
    return (x < static_cast<T>(0)) ? static_cast<T>(0ULL - as<u64>(x)) : x;
}

[[nodiscard]] constexpr f32 abs(f32 x) noexcept {
    return __builtin_bit_cast(f32, __builtin_bit_cast(u32, x) & 0x7FFFFFFFu);
}

[[nodiscard]] constexpr f64 abs(f64 x) noexcept {
    return __builtin_bit_cast(f64, __builtin_bit_cast(u64, x) & 0x7FFFFFFFFFFFFFFFULL);
}

[[nodiscard]] constexpr f32 fmin(f32 a, f32 b) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_fminf(a, b);
    }
#endif
    if (is_nan(a)) return b;
    if (is_nan(b)) return a;
    if (a == 0.0f && b == 0.0f) return signbit(a) ? a : b;
    return (a < b) ? a : b;
}

[[nodiscard]] constexpr f64 fmin(f64 a, f64 b) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_fmin(a, b);
    }
#endif
    if (is_nan(a)) return b;
    if (is_nan(b)) return a;
    if (a == 0.0 && b == 0.0) return signbit(a) ? a : b;
    return (a < b) ? a : b;
}

[[nodiscard]] constexpr f32 fmax(f32 a, f32 b) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_fmaxf(a, b);
    }
#endif
    if (is_nan(a)) return b;
    if (is_nan(b)) return a;
    if (a == 0.0f && b == 0.0f) return signbit(a) ? b : a;
    return (a > b) ? a : b;
}

[[nodiscard]] constexpr f64 fmax(f64 a, f64 b) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_fmax(a, b);
    }
#endif
    if (is_nan(a)) return b;
    if (is_nan(b)) return a;
    if (a == 0.0 && b == 0.0) return signbit(a) ? b : a;
    return (a > b) ? a : b;
}

template <typename T>
    requires(IsIntegral<T> || IsFloatingPoint<T>)
[[nodiscard]] constexpr T sqr(T x) noexcept {
    return x * x;
}

template <typename T>
    requires(IsSigned<T> && (IsIntegral<T> || IsFloatingPoint<T>))
[[nodiscard]] constexpr T sign(T x) noexcept {
    return static_cast<T>((static_cast<T>(0) < x) - (x < static_cast<T>(0)));
}

// ============================================================================
// 4. IEEE-754 Floating-Point Rounding (constexpr + hardware-inlined runtime)
// ============================================================================

[[nodiscard]] constexpr f32 trunc(f32 x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_truncf(x);
    }
#endif
    if (!is_finite(x) || abs(x) >= 8388608.0f) return x;
    const f32 r = as<f32>(as<i32>(x));
    return (r == 0.0f) ? copysign(0.0f, x) : r;
}

[[nodiscard]] constexpr f64 trunc(f64 x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_trunc(x);
    }
#endif
    if (!is_finite(x) || abs(x) >= 4503599627370496.0) return x;
    const f64 r = as<f64>(as<i64>(x));
    return (r == 0.0) ? copysign(0.0, x) : r;
}

[[nodiscard]] constexpr f32 floor(f32 x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_floorf(x);
    }
#endif
    if (!is_finite(x) || abs(x) >= 8388608.0f) return x;
    const f32 t = trunc(x);
    return (t > x) ? (t - 1.0f) : t;
}

[[nodiscard]] constexpr f64 floor(f64 x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_floor(x);
    }
#endif
    if (!is_finite(x) || abs(x) >= 4503599627370496.0) return x;
    const f64 t = trunc(x);
    return (t > x) ? (t - 1.0) : t;
}

[[nodiscard]] constexpr f32 ceil(f32 x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_ceilf(x);
    }
#endif
    if (!is_finite(x) || abs(x) >= 8388608.0f) return x;
    const f32 t = trunc(x);
    return (t < x) ? (t + 1.0f) : t;
}

[[nodiscard]] constexpr f64 ceil(f64 x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_ceil(x);
    }
#endif
    if (!is_finite(x) || abs(x) >= 4503599627370496.0) return x;
    const f64 t = trunc(x);
    return (t < x) ? (t + 1.0) : t;
}

[[nodiscard]] constexpr f32 round(f32 x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_roundf(x);
    }
#endif
    if (!is_finite(x) || abs(x) >= 8388608.0f) return x;
    const f32 t = trunc(x);
    const f32 diff = x - t;
    if (diff >= 0.5f) return t + 1.0f;
    if (diff <= -0.5f) return t - 1.0f;
    return t;
}

[[nodiscard]] constexpr f64 round(f64 x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (!__builtin_is_constant_evaluated()) {
        return __builtin_round(x);
    }
#endif
    if (!is_finite(x) || abs(x) >= 4503599627370496.0) return x;
    const f64 t = trunc(x);
    const f64 diff = x - t;
    if (diff >= 0.5) return t + 1.0;
    if (diff <= -0.5) return t - 1.0;
    return t;
}

[[nodiscard]] constexpr f32 fract(f32 x) noexcept {
    return x - floor(x);
}

[[nodiscard]] constexpr f64 fract(f64 x) noexcept {
    return x - floor(x);
}

[[nodiscard]] constexpr bool is_integer(f32 x) noexcept {
    return is_finite(x) && trunc(x) == x;
}

[[nodiscard]] constexpr bool is_integer(f64 x) noexcept {
    return is_finite(x) && trunc(x) == x;
}

// ============================================================================
// 5. UB-Free Exact Integer Rounding (replaces C long/long long lround/llround)
// ============================================================================

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i32 round_i32(T x) noexcept {
    const f64 d = as<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 2147483647.0) return Limits<i32>::kMax;
    if (d <= -2147483648.0) return Limits<i32>::kMin;
    const i32 t = as<i32>(d);
    const f64 frac = d - as<f64>(t);
    if (frac >= 0.5) return t + 1;
    if (frac <= -0.5) return t - 1;
    return t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i64 round_i64(T x) noexcept {
    const f64 d = as<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 9223372036854775808.0) return Limits<i64>::kMax;
    if (d <= -9223372036854775808.0) return Limits<i64>::kMin;
    if (abs(d) >= 4503599627370496.0) return as<i64>(d);
    const i64 t = as<i64>(d);
    const f64 frac = d - as<f64>(t);
    if (frac >= 0.5) return t + 1;
    if (frac <= -0.5) return t - 1;
    return t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr u64 round_u64(T x) noexcept {
    const f64 d = as<f64>(x);
    if (is_nan(d) || d <= 0.0) return 0ULL;
    if (d >= 18446744073709551616.0) return Limits<u64>::kMax;
    if (d >= 4503599627370496.0) return as<u64>(d);
    const u64 t = as<u64>(d);
    return ((d - as<f64>(t)) >= 0.5) ? (t + 1ULL) : t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr u32 round_u32(T x) noexcept {
    const u64 r = round_u64(x);
    return (r > Limits<u32>::kMax) ? Limits<u32>::kMax : as<u32>(r);
}

// Generic round-to-nearest (ties away from zero) with saturation to [Limits<Int>::kMin, Limits<Int>::kMax]; NaN -> 0.
template <typename Int, typename Float>
    requires(IsIntegral<Int> && !IsSame<RemoveCv<Int>, bool> && IsFloatingPoint<Float>)
[[nodiscard]] constexpr RemoveCv<Int> round_sat(Float x) noexcept {
    using CleanInt = RemoveCv<Int>;
    if constexpr (IsUnsigned<CleanInt>) {
        const u64 r = round_u64(x);
        return (r > as<u64>(Limits<CleanInt>::kMax)) ? Limits<CleanInt>::kMax : static_cast<CleanInt>(r);
    } else {
        const i64 r = round_i64(x);
        if (r < as<i64>(Limits<CleanInt>::kMin)) return Limits<CleanInt>::kMin;
        if (r > as<i64>(Limits<CleanInt>::kMax)) return Limits<CleanInt>::kMax;
        return static_cast<CleanInt>(r);
    }
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i32 floor_i32(T x) noexcept {
    const f64 d = as<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 2147483647.0) return Limits<i32>::kMax;
    if (d <= -2147483648.0) return Limits<i32>::kMin;
    const i32 t = as<i32>(d);
    return (as<f64>(t) > d) ? (t - 1) : t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i64 floor_i64(T x) noexcept {
    const f64 d = as<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 9223372036854775808.0) return Limits<i64>::kMax;
    if (d <= -9223372036854775808.0) return Limits<i64>::kMin;
    if (abs(d) >= 4503599627370496.0) return as<i64>(d);
    const i64 t = as<i64>(d);
    return (as<f64>(t) > d) ? (t - 1) : t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i32 ceil_i32(T x) noexcept {
    const f64 d = as<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 2147483647.0) return Limits<i32>::kMax;
    if (d <= -2147483648.0) return Limits<i32>::kMin;
    const i32 t = as<i32>(d);
    return (as<f64>(t) < d) ? (t + 1) : t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i64 ceil_i64(T x) noexcept {
    const f64 d = as<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 9223372036854775808.0) return Limits<i64>::kMax;
    if (d <= -9223372036854775808.0) return Limits<i64>::kMin;
    if (abs(d) >= 4503599627370496.0) return as<i64>(d);
    const i64 t = as<i64>(d);
    return (as<f64>(t) < d) ? (t + 1) : t;
}

// ============================================================================
// 6. Elementary Scalar Math (f32 / f64, zero <cmath> in header)
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)

[[nodiscard]] inline f32 sqrt(f32 x) noexcept { return __builtin_sqrtf(x); }
[[nodiscard]] inline f64 sqrt(f64 x) noexcept { return __builtin_sqrt(x); }
[[nodiscard]] inline f32 cbrt(f32 x) noexcept { return __builtin_cbrtf(x); }
[[nodiscard]] inline f64 cbrt(f64 x) noexcept { return __builtin_cbrt(x); }
[[nodiscard]] inline f32 hypot(f32 x, f32 y) noexcept { return __builtin_hypotf(x, y); }
[[nodiscard]] inline f64 hypot(f64 x, f64 y) noexcept { return __builtin_hypot(x, y); }
[[nodiscard]] inline f32 pow(f32 b, f32 e) noexcept { return __builtin_powf(b, e); }
[[nodiscard]] inline f64 pow(f64 b, f64 e) noexcept { return __builtin_pow(b, e); }
[[nodiscard]] inline f32 fma(f32 a, f32 b, f32 c) noexcept { return __builtin_fmaf(a, b, c); }
[[nodiscard]] inline f64 fma(f64 a, f64 b, f64 c) noexcept { return __builtin_fma(a, b, c); }
[[nodiscard]] inline f32 fmod(f32 x, f32 y) noexcept { return __builtin_fmodf(x, y); }
[[nodiscard]] inline f64 fmod(f64 x, f64 y) noexcept { return __builtin_fmod(x, y); }
[[nodiscard]] inline f32 remainder(f32 x, f32 y) noexcept { return __builtin_remainderf(x, y); }
[[nodiscard]] inline f64 remainder(f64 x, f64 y) noexcept { return __builtin_remainder(x, y); }

[[nodiscard]] inline f32 exp(f32 x) noexcept { return __builtin_expf(x); }
[[nodiscard]] inline f64 exp(f64 x) noexcept { return __builtin_exp(x); }
[[nodiscard]] inline f32 exp2(f32 x) noexcept { return __builtin_exp2f(x); }
[[nodiscard]] inline f64 exp2(f64 x) noexcept { return __builtin_exp2(x); }
[[nodiscard]] inline f32 expm1(f32 x) noexcept { return __builtin_expm1f(x); }
[[nodiscard]] inline f64 expm1(f64 x) noexcept { return __builtin_expm1(x); }

[[nodiscard]] inline f32 log(f32 x) noexcept { return __builtin_logf(x); }
[[nodiscard]] inline f64 log(f64 x) noexcept { return __builtin_log(x); }
[[nodiscard]] inline f32 log2(f32 x) noexcept { return __builtin_log2f(x); }
[[nodiscard]] inline f64 log2(f64 x) noexcept { return __builtin_log2(x); }
[[nodiscard]] inline f32 log10(f32 x) noexcept { return __builtin_log10f(x); }
[[nodiscard]] inline f64 log10(f64 x) noexcept { return __builtin_log10(x); }
[[nodiscard]] inline f32 log1p(f32 x) noexcept { return __builtin_log1pf(x); }
[[nodiscard]] inline f64 log1p(f64 x) noexcept { return __builtin_log1p(x); }

[[nodiscard]] inline f32 sin(f32 x) noexcept { return __builtin_sinf(x); }
[[nodiscard]] inline f64 sin(f64 x) noexcept { return __builtin_sin(x); }
[[nodiscard]] inline f32 cos(f32 x) noexcept { return __builtin_cosf(x); }
[[nodiscard]] inline f64 cos(f64 x) noexcept { return __builtin_cos(x); }
[[nodiscard]] inline f32 tan(f32 x) noexcept { return __builtin_tanf(x); }
[[nodiscard]] inline f64 tan(f64 x) noexcept { return __builtin_tan(x); }

[[nodiscard]] inline f32 asin(f32 x) noexcept { return __builtin_asinf(x); }
[[nodiscard]] inline f64 asin(f64 x) noexcept { return __builtin_asin(x); }
[[nodiscard]] inline f32 acos(f32 x) noexcept { return __builtin_acosf(x); }
[[nodiscard]] inline f64 acos(f64 x) noexcept { return __builtin_acos(x); }
[[nodiscard]] inline f32 atan(f32 x) noexcept { return __builtin_atanf(x); }
[[nodiscard]] inline f64 atan(f64 x) noexcept { return __builtin_atan(x); }
[[nodiscard]] inline f32 atan2(f32 y, f32 x) noexcept { return __builtin_atan2f(y, x); }
[[nodiscard]] inline f64 atan2(f64 y, f64 x) noexcept { return __builtin_atan2(y, x); }

[[nodiscard]] inline f32 sinh(f32 x) noexcept { return __builtin_sinhf(x); }
[[nodiscard]] inline f64 sinh(f64 x) noexcept { return __builtin_sinh(x); }
[[nodiscard]] inline f32 cosh(f32 x) noexcept { return __builtin_coshf(x); }
[[nodiscard]] inline f64 cosh(f64 x) noexcept { return __builtin_cosh(x); }
[[nodiscard]] inline f32 tanh(f32 x) noexcept { return __builtin_tanhf(x); }
[[nodiscard]] inline f64 tanh(f64 x) noexcept { return __builtin_tanh(x); }

[[nodiscard]] inline f32 asinh(f32 x) noexcept { return __builtin_asinhf(x); }
[[nodiscard]] inline f64 asinh(f64 x) noexcept { return __builtin_asinh(x); }
[[nodiscard]] inline f32 acosh(f32 x) noexcept { return __builtin_acoshf(x); }
[[nodiscard]] inline f64 acosh(f64 x) noexcept { return __builtin_acosh(x); }
[[nodiscard]] inline f32 atanh(f32 x) noexcept { return __builtin_atanhf(x); }
[[nodiscard]] inline f64 atanh(f64 x) noexcept { return __builtin_atanh(x); }

[[nodiscard]] inline f32 erf(f32 x) noexcept { return __builtin_erff(x); }
[[nodiscard]] inline f64 erf(f64 x) noexcept { return __builtin_erf(x); }
[[nodiscard]] inline f32 erfc(f32 x) noexcept { return __builtin_erfcf(x); }
[[nodiscard]] inline f64 erfc(f64 x) noexcept { return __builtin_erfc(x); }
[[nodiscard]] inline f32 tgamma(f32 x) noexcept { return __builtin_tgammaf(x); }
[[nodiscard]] inline f64 tgamma(f64 x) noexcept { return __builtin_tgamma(x); }
[[nodiscard]] inline f32 lgamma(f32 x) noexcept { return __builtin_lgammaf(x); }
[[nodiscard]] inline f64 lgamma(f64 x) noexcept { return __builtin_lgamma(x); }
[[nodiscard]] inline f32 nextafter(f32 from, f32 to) noexcept { return __builtin_nextafterf(from, to); }
[[nodiscard]] inline f64 nextafter(f64 from, f64 to) noexcept { return __builtin_nextafter(from, to); }
[[nodiscard]] inline f32 nearbyint(f32 x) noexcept { return __builtin_nearbyintf(x); }
[[nodiscard]] inline f64 nearbyint(f64 x) noexcept { return __builtin_nearbyint(x); }
[[nodiscard]] inline f32 rint(f32 x) noexcept { return __builtin_rintf(x); }
[[nodiscard]] inline f64 rint(f64 x) noexcept { return __builtin_rint(x); }
[[nodiscard]] inline f32 fdim(f32 x, f32 y) noexcept { return __builtin_fdimf(x, y); }
[[nodiscard]] inline f64 fdim(f64 x, f64 y) noexcept { return __builtin_fdim(x, y); }

[[nodiscard]] inline f32 ldexp(f32 x, i32 exp_val) noexcept { return __builtin_ldexpf(x, as<int>(exp_val)); }
[[nodiscard]] inline f64 ldexp(f64 x, i32 exp_val) noexcept { return __builtin_ldexp(x, as<int>(exp_val)); }

[[nodiscard]] inline f32 frexp(f32 x, i32* exp_out) noexcept {
    int e = 0;
    const f32 m = __builtin_frexpf(x, &e);
    if (exp_out) *exp_out = as<i32>(e);
    return m;
}

[[nodiscard]] inline f64 frexp(f64 x, i32* exp_out) noexcept {
    int e = 0;
    const f64 m = __builtin_frexp(x, &e);
    if (exp_out) *exp_out = as<i32>(e);
    return m;
}

[[nodiscard]] inline f32 modf(f32 x, f32* iptr) noexcept {
    f32 ip = 0.0f;
    const f32 frac = __builtin_modff(x, &ip);
    if (iptr) *iptr = ip;
    return frac;
}

[[nodiscard]] inline f64 modf(f64 x, f64* iptr) noexcept {
    f64 ip = 0.0;
    const f64 frac = __builtin_modf(x, &ip);
    if (iptr) *iptr = ip;
    return frac;
}

#else

[[nodiscard]] f32 sqrt(f32 x) noexcept;
[[nodiscard]] f64 sqrt(f64 x) noexcept;
[[nodiscard]] f32 cbrt(f32 x) noexcept;
[[nodiscard]] f64 cbrt(f64 x) noexcept;
[[nodiscard]] f32 hypot(f32 x, f32 y) noexcept;
[[nodiscard]] f64 hypot(f64 x, f64 y) noexcept;
[[nodiscard]] f32 pow(f32 b, f32 e) noexcept;
[[nodiscard]] f64 pow(f64 b, f64 e) noexcept;
[[nodiscard]] f32 fma(f32 a, f32 b, f32 c) noexcept;
[[nodiscard]] f64 fma(f64 a, f64 b, f64 c) noexcept;
[[nodiscard]] f32 fmod(f32 x, f32 y) noexcept;
[[nodiscard]] f64 fmod(f64 x, f64 y) noexcept;
[[nodiscard]] f32 remainder(f32 x, f32 y) noexcept;
[[nodiscard]] f64 remainder(f64 x, f64 y) noexcept;

[[nodiscard]] f32 exp(f32 x) noexcept;
[[nodiscard]] f64 exp(f64 x) noexcept;
[[nodiscard]] f32 exp2(f32 x) noexcept;
[[nodiscard]] f64 exp2(f64 x) noexcept;
[[nodiscard]] f32 expm1(f32 x) noexcept;
[[nodiscard]] f64 expm1(f64 x) noexcept;

[[nodiscard]] f32 log(f32 x) noexcept;
[[nodiscard]] f64 log(f64 x) noexcept;
[[nodiscard]] f32 log2(f32 x) noexcept;
[[nodiscard]] f64 log2(f64 x) noexcept;
[[nodiscard]] f32 log10(f32 x) noexcept;
[[nodiscard]] f64 log10(f64 x) noexcept;
[[nodiscard]] f32 log1p(f32 x) noexcept;
[[nodiscard]] f64 log1p(f64 x) noexcept;

[[nodiscard]] f32 sin(f32 x) noexcept;
[[nodiscard]] f64 sin(f64 x) noexcept;
[[nodiscard]] f32 cos(f32 x) noexcept;
[[nodiscard]] f64 cos(f64 x) noexcept;
[[nodiscard]] f32 tan(f32 x) noexcept;
[[nodiscard]] f64 tan(f64 x) noexcept;

[[nodiscard]] f32 asin(f32 x) noexcept;
[[nodiscard]] f64 asin(f64 x) noexcept;
[[nodiscard]] f32 acos(f32 x) noexcept;
[[nodiscard]] f64 acos(f64 x) noexcept;
[[nodiscard]] f32 atan(f32 x) noexcept;
[[nodiscard]] f64 atan(f64 x) noexcept;
[[nodiscard]] f32 atan2(f32 y, f32 x) noexcept;
[[nodiscard]] f64 atan2(f64 y, f64 x) noexcept;

[[nodiscard]] f32 sinh(f32 x) noexcept;
[[nodiscard]] f64 sinh(f64 x) noexcept;
[[nodiscard]] f32 cosh(f32 x) noexcept;
[[nodiscard]] f64 cosh(f64 x) noexcept;
[[nodiscard]] f32 tanh(f32 x) noexcept;
[[nodiscard]] f64 tanh(f64 x) noexcept;

[[nodiscard]] f32 asinh(f32 x) noexcept;
[[nodiscard]] f64 asinh(f64 x) noexcept;
[[nodiscard]] f32 acosh(f32 x) noexcept;
[[nodiscard]] f64 acosh(f64 x) noexcept;
[[nodiscard]] f32 atanh(f32 x) noexcept;
[[nodiscard]] f64 atanh(f64 x) noexcept;

[[nodiscard]] f32 erf(f32 x) noexcept;
[[nodiscard]] f64 erf(f64 x) noexcept;
[[nodiscard]] f32 erfc(f32 x) noexcept;
[[nodiscard]] f64 erfc(f64 x) noexcept;
[[nodiscard]] f32 tgamma(f32 x) noexcept;
[[nodiscard]] f64 tgamma(f64 x) noexcept;
[[nodiscard]] f32 lgamma(f32 x) noexcept;
[[nodiscard]] f64 lgamma(f64 x) noexcept;
[[nodiscard]] f32 nextafter(f32 from, f32 to) noexcept;
[[nodiscard]] f64 nextafter(f64 from, f64 to) noexcept;
[[nodiscard]] f32 nearbyint(f32 x) noexcept;
[[nodiscard]] f64 nearbyint(f64 x) noexcept;
[[nodiscard]] f32 rint(f32 x) noexcept;
[[nodiscard]] f64 rint(f64 x) noexcept;
[[nodiscard]] f32 fdim(f32 x, f32 y) noexcept;
[[nodiscard]] f64 fdim(f64 x, f64 y) noexcept;

[[nodiscard]] f32 ldexp(f32 x, i32 exp_val) noexcept;
[[nodiscard]] f64 ldexp(f64 x, i32 exp_val) noexcept;
[[nodiscard]] f32 frexp(f32 x, i32* exp_out) noexcept;
[[nodiscard]] f64 frexp(f64 x, i32* exp_out) noexcept;
[[nodiscard]] f32 modf(f32 x, f32* iptr) noexcept;
[[nodiscard]] f64 modf(f64 x, f64* iptr) noexcept;

#endif

// ============================================================================
// 7. Interpolation, Smoothstep & Approximate Equality
// ============================================================================

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr T lerp(T a, T b, T t) noexcept {
    return a + (b - a) * t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr T inverse_lerp(T a, T b, T v) noexcept {
    const T span = b - a;
    return (span != static_cast<T>(0)) ? ((v - a) / span) : static_cast<T>(0);
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr T remap(T v, T in_min, T in_max, T out_min, T out_max) noexcept {
    return lerp(out_min, out_max, inverse_lerp(in_min, in_max, v));
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr T smoothstep(T edge0, T edge1, T x) noexcept {
    const T span = edge1 - edge0;
    if (span == static_cast<T>(0)) {
        return (x < edge0) ? static_cast<T>(0) : static_cast<T>(1);
    }
    const T t = clamp((x - edge0) / span, static_cast<T>(0), static_cast<T>(1));
    return t * t * (static_cast<T>(3) - static_cast<T>(2) * t);
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr bool approx_eq(T a, T b, T eps = static_cast<T>(1e-5)) noexcept {
    return abs(a - b) <= eps;
}

} // namespace keel
