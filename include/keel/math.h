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
// 2. Numeric Limits (replaces <limits> and C macros NAN / HUGE_VAL / INT_MAX)
// ============================================================================

template <typename T> struct Limits;

template <typename T>
    requires(IsIntegral<T> && !IsSame<RemoveCv<T>, bool>)
struct Limits<T> {
    using CleanT = RemoveCv<T>;
    static constexpr usize kBits = sizeof(CleanT) * 8;
    static constexpr CleanT kMin =
        IsSigned<CleanT> ? static_cast<CleanT>(static_cast<u64>(1) << (kBits - 1)) : static_cast<CleanT>(0);
    static constexpr CleanT kLowest = kMin;
    static constexpr CleanT kMax =
        IsSigned<CleanT> ? static_cast<CleanT>((~u64{0}) >> (65 - kBits)) : static_cast<CleanT>((~u64{0}) >> (64 - kBits));
};

template <> struct Limits<f32> {
    static constexpr f32 kMax = __builtin_bit_cast(f32, 0x7F7FFFFFu);
    static constexpr f32 kLowest = -kMax;
    static constexpr f32 kMinPositive = __builtin_bit_cast(f32, 0x00800000u);
    static constexpr f32 kEpsilon = __builtin_bit_cast(f32, 0x34000000u); // 2^-23
    [[nodiscard]] static constexpr f32 infinity() noexcept { return __builtin_huge_valf(); }
    [[nodiscard]] static constexpr f32 quiet_nan() noexcept { return __builtin_nanf(""); }
};

template <> struct Limits<f64> {
    static constexpr f64 kMax = __builtin_bit_cast(f64, 0x7FEFFFFFFFFFFFFFULL);
    static constexpr f64 kLowest = -kMax;
    static constexpr f64 kMinPositive = __builtin_bit_cast(f64, 0x0010000000000000ULL);
    static constexpr f64 kEpsilon = __builtin_bit_cast(f64, 0x3CB0000000000000ULL); // 2^-52
    [[nodiscard]] static constexpr f64 infinity() noexcept { return __builtin_huge_val(); }
    [[nodiscard]] static constexpr f64 quiet_nan() noexcept { return __builtin_nan(""); }
};

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
    requires(IsSigned<T> && IsIntegral<T>)
[[nodiscard]] constexpr T abs(T x) noexcept {
    return (x < static_cast<T>(0)) ? static_cast<T>(0ULL - static_cast<u64>(x)) : x;
}

[[nodiscard]] constexpr f32 abs(f32 x) noexcept {
    return __builtin_bit_cast(f32, __builtin_bit_cast(u32, x) & 0x7FFFFFFFu);
}

[[nodiscard]] constexpr f64 abs(f64 x) noexcept {
    return __builtin_bit_cast(f64, __builtin_bit_cast(u64, x) & 0x7FFFFFFFFFFFFFFFULL);
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
// 4. UB-Free Exact Integer Rounding (replaces C long/long long lround/llround)
// ============================================================================

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i32 round_i32(T x) noexcept {
    const f64 d = static_cast<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 2147483647.0) return Limits<i32>::kMax;
    if (d <= -2147483648.0) return Limits<i32>::kMin;
    const i32 t = static_cast<i32>(d);
    const f64 frac = d - static_cast<f64>(t);
    if (frac >= 0.5) return t + 1;
    if (frac <= -0.5) return t - 1;
    return t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i64 round_i64(T x) noexcept {
    const f64 d = static_cast<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 9223372036854775808.0) return Limits<i64>::kMax;
    if (d <= -9223372036854775808.0) return Limits<i64>::kMin;
    if (abs(d) >= 4503599627370496.0) return static_cast<i64>(d);
    const i64 t = static_cast<i64>(d);
    const f64 frac = d - static_cast<f64>(t);
    if (frac >= 0.5) return t + 1;
    if (frac <= -0.5) return t - 1;
    return t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr u64 round_u64(T x) noexcept {
    const f64 d = static_cast<f64>(x);
    if (is_nan(d) || d <= 0.0) return 0ULL;
    if (d >= 18446744073709551616.0) return Limits<u64>::kMax;
    if (d >= 4503599627370496.0) return static_cast<u64>(d);
    const u64 t = static_cast<u64>(d);
    return ((d - static_cast<f64>(t)) >= 0.5) ? (t + 1ULL) : t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr u32 round_u32(T x) noexcept {
    const u64 r = round_u64(x);
    return (r > Limits<u32>::kMax) ? Limits<u32>::kMax : static_cast<u32>(r);
}

// Generic round-to-nearest (ties away from zero) with saturation to [Limits<Int>::kMin, Limits<Int>::kMax]; NaN -> 0.
template <typename Int, typename Float>
    requires(IsIntegral<Int> && !IsSame<RemoveCv<Int>, bool> && IsFloatingPoint<Float>)
[[nodiscard]] constexpr RemoveCv<Int> round_sat(Float x) noexcept {
    using CleanInt = RemoveCv<Int>;
    if constexpr (IsUnsigned<CleanInt>) {
        const u64 r = round_u64(x);
        return (r > static_cast<u64>(Limits<CleanInt>::kMax)) ? Limits<CleanInt>::kMax : static_cast<CleanInt>(r);
    } else {
        const i64 r = round_i64(x);
        if (r < static_cast<i64>(Limits<CleanInt>::kMin)) return Limits<CleanInt>::kMin;
        if (r > static_cast<i64>(Limits<CleanInt>::kMax)) return Limits<CleanInt>::kMax;
        return static_cast<CleanInt>(r);
    }
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i32 floor_i32(T x) noexcept {
    const f64 d = static_cast<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 2147483647.0) return Limits<i32>::kMax;
    if (d <= -2147483648.0) return Limits<i32>::kMin;
    const i32 t = static_cast<i32>(d);
    return (static_cast<f64>(t) > d) ? (t - 1) : t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i64 floor_i64(T x) noexcept {
    const f64 d = static_cast<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 9223372036854775808.0) return Limits<i64>::kMax;
    if (d <= -9223372036854775808.0) return Limits<i64>::kMin;
    if (abs(d) >= 4503599627370496.0) return static_cast<i64>(d);
    const i64 t = static_cast<i64>(d);
    return (static_cast<f64>(t) > d) ? (t - 1) : t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i32 ceil_i32(T x) noexcept {
    const f64 d = static_cast<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 2147483647.0) return Limits<i32>::kMax;
    if (d <= -2147483648.0) return Limits<i32>::kMin;
    const i32 t = static_cast<i32>(d);
    return (static_cast<f64>(t) < d) ? (t + 1) : t;
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr i64 ceil_i64(T x) noexcept {
    const f64 d = static_cast<f64>(x);
    if (is_nan(d)) return 0;
    if (d >= 9223372036854775808.0) return Limits<i64>::kMax;
    if (d <= -9223372036854775808.0) return Limits<i64>::kMin;
    if (abs(d) >= 4503599627370496.0) return static_cast<i64>(d);
    const i64 t = static_cast<i64>(d);
    return (static_cast<f64>(t) < d) ? (t + 1) : t;
}

// ============================================================================
// 5. Interpolation & Approximate Equality
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
[[nodiscard]] constexpr bool approx_eq(T a, T b, T eps = static_cast<T>(1e-5)) noexcept {
    return abs(a - b) <= eps;
}

} // namespace keel
