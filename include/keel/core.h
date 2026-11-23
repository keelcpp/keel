#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>

#if defined(__clang__)
#define KEEL_LIFETIMEBOUND [[clang::lifetimebound]]
#elif defined(_MSC_VER) && _MSC_VER >= 1930
#define KEEL_LIFETIMEBOUND [[msvc::lifetimebound]]
#else
#define KEEL_LIFETIMEBOUND
#endif

#if defined(_MSC_VER)
#define KEEL_FORCEINLINE __forceinline
#define KEEL_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define KEEL_FORCEINLINE inline __attribute__((always_inline))
#define KEEL_NOINLINE __attribute__((noinline))
#else
#define KEEL_FORCEINLINE inline
#define KEEL_NOINLINE
#endif

namespace keel {

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using f32 = float;
using f64 = double;

using isize = std::ptrdiff_t;
using usize = std::size_t;

static_assert(sizeof(isize) == sizeof(void*), "isize must match pointer size");
static_assert(sizeof(usize) == sizeof(void*), "usize must match pointer size");

inline constexpr usize kInvalidIndex = static_cast<usize>(0xFFFFFFFFFFFFFFFFULL);
inline constexpr usize npos = kInvalidIndex;

namespace detail {
[[noreturn]] void assert_fail(const char* expr, const char* file, i32 line) noexcept;
} // namespace detail

#if defined(KEEL_DISABLE_ASSERTS)
#define KEEL_ASSERT(cond) ((void)0)
#else
#define KEEL_ASSERT(cond)                                                                                              \
    do {                                                                                                               \
        if (!(cond)) [[unlikely]] {                                                                                    \
            ::keel::detail::assert_fail(#cond, __FILE__, __LINE__);                                                    \
        }                                                                                                              \
    } while (0)
#endif

#if defined(KEEL_NO_BOUNDS_CHECK) || defined(KEEL_DISABLE_ASSERTS)
#define KEEL_BOUNDS_ASSERT(cond) ((void)0)
#else
#define KEEL_BOUNDS_ASSERT(cond) KEEL_ASSERT(cond)
#endif

// Type traits implemented directly via language rules and compiler builtins (no <type_traits>)
namespace detail {
template <typename T> struct RemoveRef {
    using Type = T;
};
template <typename T> struct RemoveRef<T&> {
    using Type = T;
};
template <typename T> struct RemoveRef<T&&> {
    using Type = T;
};

template <typename T> struct RemoveConst {
    using Type = T;
};
template <typename T> struct RemoveConst<const T> {
    using Type = T;
};

template <typename T> struct RemoveCv {
    using Type = T;
};
template <typename T> struct RemoveCv<const T> {
    using Type = T;
};
template <typename T> struct RemoveCv<volatile T> {
    using Type = T;
};
template <typename T> struct RemoveCv<const volatile T> {
    using Type = T;
};

template <bool B, typename T, typename F> struct Conditional {
    using Type = T;
};
template <typename T, typename F> struct Conditional<false, T, F> {
    using Type = F;
};
} // namespace detail

template <typename T> using RemoveReference = typename detail::RemoveRef<T>::Type;
template <typename T> using RemoveConst = typename detail::RemoveConst<T>::Type;
template <typename T> using RemoveCv = typename detail::RemoveCv<T>::Type;
template <typename T> using RemoveCvRef = RemoveCv<RemoveReference<T>>;
template <bool B, typename T, typename F> using Conditional = typename detail::Conditional<B, T, F>::Type;

template <typename T, typename U> inline constexpr bool IsSame = false;
template <typename T> inline constexpr bool IsSame<T, T> = true;
template <typename T, typename U> concept SameAs = IsSame<T, U> && IsSame<U, T>;

template <typename T> inline constexpr bool IsConst = false;
template <typename T> inline constexpr bool IsConst<const T> = true;

template <typename T> inline constexpr bool IsReference = false;
template <typename T> inline constexpr bool IsReference<T&> = true;
template <typename T> inline constexpr bool IsReference<T&&> = true;

template <typename T> inline constexpr bool IsPointer = false;
template <typename T> inline constexpr bool IsPointer<T*> = true;
template <typename T> inline constexpr bool IsPointer<T* const> = true;
template <typename T> inline constexpr bool IsPointer<T* volatile> = true;
template <typename T> inline constexpr bool IsPointer<T* const volatile> = true;

namespace detail {
template <typename T> struct RemovePtr {
    using Type = T;
};
template <typename T> struct RemovePtr<T*> {
    using Type = T;
};
} // namespace detail

template <typename T> using RemovePointer = typename detail::RemovePtr<RemoveCv<T>>::Type;
template <typename T> inline constexpr bool IsFunction = !IsConst<const T> && !IsReference<T>;

namespace detail {
template <typename U, bool IsFn = IsFunction<U>> struct DecayImpl {
    using Type = typename RemoveCv<U>::Type;
};
template <typename U> struct DecayImpl<U, true> {
    using Type = U*;
};
template <typename V> struct DecayImpl<V[], false> {
    using Type = V*;
};
template <typename V, usize M> struct DecayImpl<V[M], false> {
    using Type = V*;
};
template <typename V> struct DecayImpl<const V[], false> {
    using Type = const V*;
};
template <typename V, usize M> struct DecayImpl<const V[M], false> {
    using Type = const V*;
};
} // namespace detail

template <typename T> using Decay = typename detail::DecayImpl<RemoveReference<T>>::Type;

template <typename T> inline constexpr bool IsTriviallyCopyable = __is_trivially_copyable(T);
#if defined(__clang__) || defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 11)
template <typename T> inline constexpr bool IsTriviallyDestructible = __is_trivially_destructible(T);
#else
template <typename T> inline constexpr bool IsTriviallyDestructible = __has_trivial_destructor(T);
#endif
template <typename T, typename... Args>
inline constexpr bool IsTriviallyConstructible = __is_trivially_constructible(T, Args...);
template <typename T, typename... Args> inline constexpr bool IsConstructible = __is_constructible(T, Args...);
template <typename T> inline constexpr bool IsCopyConstructible = __is_constructible(T, const T&);
template <typename T> inline constexpr bool IsNothrowMoveConstructible = __is_nothrow_constructible(T, T&&);
template <typename T> inline constexpr bool IsNothrowDestructible = requires(T& v) {
    { v.~T() } noexcept;
};
template <typename T> inline constexpr bool IsEnum = __is_enum(T);

template <typename T>
inline constexpr bool IsIntegral =
    IsSame<RemoveCv<T>, bool> || IsSame<RemoveCv<T>, char> || IsSame<RemoveCv<T>, signed char> ||
    IsSame<RemoveCv<T>, unsigned char> || IsSame<RemoveCv<T>, short> || IsSame<RemoveCv<T>, unsigned short> ||
    IsSame<RemoveCv<T>, int> || IsSame<RemoveCv<T>, unsigned int> || IsSame<RemoveCv<T>, long> ||
    IsSame<RemoveCv<T>, unsigned long> || IsSame<RemoveCv<T>, long long> || IsSame<RemoveCv<T>, unsigned long long>;

template <typename T>
inline constexpr bool IsFloatingPoint =
    IsSame<RemoveCv<T>, float> || IsSame<RemoveCv<T>, double> || IsSame<RemoveCv<T>, long double>;

template <typename T>
inline constexpr bool IsArithmetic = IsIntegral<T> || IsFloatingPoint<T>;

template <typename Base, typename Derived> inline constexpr bool IsBaseOf = __is_base_of(Base, Derived);

namespace detail {
template <typename T, bool = (IsIntegral<T> || IsFloatingPoint<T>) && !IsSame<RemoveCv<T>, bool>>
struct IsSignedImpl {
    static constexpr bool value = false;
};
template <typename T>
struct IsSignedImpl<T, true> {
    static constexpr bool value = static_cast<keel::RemoveCv<T>>(-1) < static_cast<keel::RemoveCv<T>>(0);
};

template <typename T, bool = IsEnum<T>> struct UnderlyingTypeImpl {
    using Type = T;
};
template <typename T> struct UnderlyingTypeImpl<T, true> {
    using Type = __underlying_type(T);
};

template <typename T> T&& declval() noexcept;
template <typename To> void accept_implicit(To) noexcept;

template <typename From, typename To>
concept ImplicitlyConvertibleImpl = requires { detail::accept_implicit<To>(detail::declval<From>()); };

template <typename T> struct TypeIdAnchor {
    static inline constexpr char kTag = 0;
};
} // namespace detail

template <typename T> inline constexpr bool IsSigned = detail::IsSignedImpl<T>::value;

template <typename T> inline constexpr bool IsUnsigned = IsIntegral<T> && !IsSigned<T>;

template <typename E>
    requires IsEnum<E>
using UnderlyingType = typename detail::UnderlyingTypeImpl<RemoveCv<E>>::Type;

template <typename E>
    requires IsEnum<E>
[[nodiscard]] constexpr UnderlyingType<E> to_underlying(E val) noexcept {
    return static_cast<UnderlyingType<E>>(val);
}

template <typename From, typename To>
inline constexpr bool IsConvertible =
    detail::ImplicitlyConvertibleImpl<From, To> || (IsSame<RemoveCv<From>, void> && IsSame<RemoveCv<To>, void>);

// Zero-header source location replacement for <source_location> (16 bytes on 64-bit).
struct SourceLocation {
    const char* m_file = "";
    u32 m_line = 0;
    u32 m_column = 0;

    constexpr SourceLocation() noexcept = default;
    constexpr SourceLocation(const char* file, u32 line, u32 column = 0) noexcept
        : m_file(file ? file : ""), m_line(line), m_column(column) {}

    [[nodiscard]] static consteval SourceLocation current(
        const char* file = __builtin_FILE(),
        u32 line = static_cast<u32>(__builtin_LINE()),
        u32 column = static_cast<u32>(__builtin_COLUMN())) noexcept {
        return SourceLocation(file, line, column);
    }

    [[nodiscard]] constexpr const char* file_name() const noexcept { return m_file; }
    [[nodiscard]] constexpr u32 line() const noexcept { return m_line; }
    [[nodiscard]] constexpr u32 column() const noexcept { return m_column; }

    [[nodiscard]] constexpr bool operator==(const SourceLocation& other) const noexcept = default;
};

// Zero-RTTI unique type identifier (works with -fno-rtti and constexpr).
struct TypeId {
    const void* ptr = nullptr;

    constexpr TypeId() noexcept = default;
    explicit constexpr TypeId(const void* p) noexcept : ptr(p) {}

    [[nodiscard]] usize hash_code() const noexcept { return reinterpret_cast<usize>(ptr); }
    [[nodiscard]] friend constexpr bool operator==(TypeId a, TypeId b) noexcept { return a.ptr == b.ptr; }
    [[nodiscard]] friend constexpr bool operator!=(TypeId a, TypeId b) noexcept { return a.ptr != b.ptr; }
};

template <typename T> [[nodiscard]] constexpr TypeId type_id() noexcept {
    return TypeId(&detail::TypeIdAnchor<RemoveCvRef<T>>::kTag);
}

template <typename T> [[nodiscard]] constexpr RemoveReference<T>&& move(T&& val) noexcept {
    return static_cast<RemoveReference<T>&&>(val);
}

template <typename T> [[nodiscard]] constexpr T* addressof(T& val) noexcept {
    return __builtin_addressof(val);
}

template <typename T> void addressof(const T&&) = delete;

template <typename T> [[nodiscard]] constexpr T&& forward(RemoveReference<T>& val) noexcept {
    return static_cast<T&&>(val);
}

template <typename T> [[nodiscard]] constexpr T&& forward(RemoveReference<T>&& val) noexcept {
    return static_cast<T&&>(val);
}

namespace detail {
struct SwapFn {
    template <typename T>
        requires(IsConstructible<T, T&&> && requires(T& lhs, T&& rhs) { lhs = static_cast<T&&>(rhs); })
    constexpr void operator()(T& a, T& b) const noexcept {
        T tmp = static_cast<T&&>(a);
        a = static_cast<T&&>(b);
        b = static_cast<T&&>(tmp);
    }

    template <typename T, usize N>
        requires requires(const SwapFn& fn, T& a, T& b) { fn(a, b); }
    constexpr void operator()(T (&a)[N], T (&b)[N]) const noexcept {
        for (usize i = 0; i < N; ++i) (*this)(a[i], b[i]);
    }
};
} // namespace detail

// Niebloid function object: prevents ADL ambiguity with `using std::swap; swap(a, b);`
// on types defined in `namespace keel` (so `std::is_swappable_v<keel::T>` holds everywhere).
inline constexpr detail::SwapFn swap{};

template <typename T, typename U = T> constexpr T exchange(T& obj, U&& new_val) noexcept {
    T old_val = keel::move(obj);
    obj = keel::forward<U>(new_val);
    return old_val;
}

template <typename T> [[nodiscard]] constexpr const T& min(const T& a, const T& b) noexcept {
    return (b < a) ? b : a;
}

template <typename T> [[nodiscard]] constexpr T min(std::initializer_list<T> ilist) noexcept {
    KEEL_ASSERT(ilist.size() > 0);
    const T* best = ilist.begin();
    for (const T* it = ilist.begin() + 1; it != ilist.end(); ++it) {
        if (*it < *best) best = it;
    }
    return *best;
}

template <typename T> [[nodiscard]] constexpr const T& max(const T& a, const T& b) noexcept {
    return (a < b) ? b : a;
}

template <typename T> [[nodiscard]] constexpr T max(std::initializer_list<T> ilist) noexcept {
    KEEL_ASSERT(ilist.size() > 0);
    const T* best = ilist.begin();
    for (const T* it = ilist.begin() + 1; it != ilist.end(); ++it) {
        if (*best < *it) best = it;
    }
    return *best;
}

template <typename T> [[nodiscard]] constexpr const T& clamp(const T& v, const T& lo, const T& hi) noexcept {
    return (v < lo) ? lo : ((hi < v) ? hi : v);
}

// ============================================================================
// Pair (aggregate; supports C++17/20 structured bindings without <tuple>)
// ============================================================================

template <typename First, typename Second> struct Pair {
    using first_type = First;
    using second_type = Second;

    First first;
    Second second;

    template <typename U1, typename U2> constexpr Pair& operator=(const Pair<U1, U2>& other) {
        first = other.first;
        second = other.second;
        return *this;
    }

    template <typename U1, typename U2> constexpr Pair& operator=(Pair<U1, U2>&& other) {
        first = keel::forward<U1>(other.first);
        second = keel::forward<U2>(other.second);
        return *this;
    }
};

template <typename First, typename Second> Pair(First, Second) -> Pair<First, Second>;

template <typename A, typename B>
[[nodiscard]] constexpr Pair<Decay<A>, Decay<B>> make_pair(A&& a, B&& b) {
    return Pair<Decay<A>, Decay<B>>{keel::forward<A>(a), keel::forward<B>(b)};
}

template <typename A1, typename B1, typename A2, typename B2>
[[nodiscard]] constexpr bool operator==(const Pair<A1, B1>& lhs, const Pair<A2, B2>& rhs) {
    return lhs.first == rhs.first && lhs.second == rhs.second;
}

template <typename A1, typename B1, typename A2, typename B2>
[[nodiscard]] constexpr bool operator!=(const Pair<A1, B1>& lhs, const Pair<A2, B2>& rhs) {
    return !(lhs == rhs);
}

template <typename A1, typename B1, typename A2, typename B2>
[[nodiscard]] constexpr bool operator<(const Pair<A1, B1>& lhs, const Pair<A2, B2>& rhs) {
    return lhs.first < rhs.first || (!(rhs.first < lhs.first) && lhs.second < rhs.second);
}

template <typename A1, typename B1, typename A2, typename B2>
[[nodiscard]] constexpr bool operator<=(const Pair<A1, B1>& lhs, const Pair<A2, B2>& rhs) {
    return !(rhs < lhs);
}

template <typename A1, typename B1, typename A2, typename B2>
[[nodiscard]] constexpr bool operator>(const Pair<A1, B1>& lhs, const Pair<A2, B2>& rhs) {
    return rhs < lhs;
}

template <typename A1, typename B1, typename A2, typename B2>
[[nodiscard]] constexpr bool operator>=(const Pair<A1, B1>& lhs, const Pair<A2, B2>& rhs) {
    return !(lhs < rhs);
}

// ============================================================================
// Explicit Arithmetic Conversions: as<To>, narrow<To>, saturate<To>
// ============================================================================

namespace detail {

template <typename T>
    requires(IsIntegral<T> && !IsSame<keel::RemoveCv<T>, bool>)
[[nodiscard]] constexpr keel::RemoveCv<T> int_min_val() noexcept {
    using CleanT = keel::RemoveCv<T>;
    if constexpr (IsSigned<CleanT>) {
        return static_cast<CleanT>(u64{1} << (sizeof(CleanT) * 8 - 1));
    } else {
        return static_cast<CleanT>(0);
    }
}

template <typename T>
    requires(IsIntegral<T> && !IsSame<keel::RemoveCv<T>, bool>)
[[nodiscard]] constexpr keel::RemoveCv<T> int_max_val() noexcept {
    using CleanT = keel::RemoveCv<T>;
    if constexpr (IsSigned<CleanT>) {
        return static_cast<CleanT>(0xFFFFFFFFFFFFFFFFULL >> (65 - sizeof(CleanT) * 8));
    } else {
        return static_cast<CleanT>(0xFFFFFFFFFFFFFFFFULL >> (64 - sizeof(CleanT) * 8));
    }
}

template <typename To, typename From>
    requires(IsArithmetic<To> && IsArithmetic<From>)
[[nodiscard]] constexpr bool in_numeric_range(From val) noexcept {
    using ToT = keel::RemoveCv<To>;
    using FromT = keel::RemoveCv<From>;

    if constexpr (IsSame<ToT, bool>) {
        return val == static_cast<FromT>(0) || val == static_cast<FromT>(1);
    } else if constexpr (IsIntegral<ToT> && IsIntegral<FromT>) {
        if constexpr (IsSame<FromT, bool>) {
            return true;
        } else if constexpr (IsSigned<FromT> == IsSigned<ToT>) {
            if constexpr (sizeof(ToT) >= sizeof(FromT)) {
                return true;
            } else {
                return val >= static_cast<FromT>(int_min_val<ToT>()) &&
                       val <= static_cast<FromT>(int_max_val<ToT>());
            }
        } else if constexpr (IsSigned<FromT> && !IsSigned<ToT>) {
            if (val < static_cast<FromT>(0)) {
                return false;
            }
            return static_cast<u64>(val) <= static_cast<u64>(int_max_val<ToT>());
        } else {
            // Unsigned -> Signed
            return static_cast<u64>(val) <= static_cast<u64>(int_max_val<ToT>());
        }
    } else if constexpr (IsIntegral<ToT> && IsFloatingPoint<FromT>) {
        if (val != val) {
            return false; // NaN
        }
        const f64 d = static_cast<f64>(val);
        if constexpr (sizeof(ToT) == 8) {
            if constexpr (IsSigned<ToT>) {
                return d >= -9223372036854775808.0 && d < 9223372036854775808.0;
            } else {
                return d >= 0.0 && d < 18446744073709551616.0;
            }
        } else {
            return d >= static_cast<f64>(int_min_val<ToT>()) && d <= static_cast<f64>(int_max_val<ToT>());
        }
    } else if constexpr (IsFloatingPoint<ToT> && IsIntegral<FromT>) {
        return true;
    } else {
        // FloatingPoint -> FloatingPoint
        if constexpr (sizeof(ToT) >= sizeof(FromT)) {
            return true;
        } else {
            if (val != val) {
                return true; // NaN preserved
            }
            const f64 d = static_cast<f64>(val);
            if (d == __builtin_huge_val() || d == -__builtin_huge_val()) {
                return true; // +/-inf preserved
            }
            constexpr f64 kF32Max = 3.4028234663852886e+38;
            return d >= -kF32Max && d <= kF32Max;
        }
    }
}

#if defined(__SIZEOF_INT128__)
template <typename T>
inline constexpr bool IsInt128 =
    IsSame<keel::RemoveCv<T>, __int128> || IsSame<keel::RemoveCv<T>, unsigned __int128>;
#else
template <typename T> inline constexpr bool IsInt128 = false;
#endif

} // namespace detail

// Explicit numeric / enum-to-int conversion (rejects pointers, int->enum, and const/reinterpret casts; zero overhead).
template <typename To, typename From>
    requires(((IsArithmetic<To> || detail::IsInt128<To>) && (IsArithmetic<From> || detail::IsInt128<From>)) ||
             (IsIntegral<To> && IsEnum<From>))
[[nodiscard]] constexpr RemoveCv<To> as(From val) noexcept {
    return static_cast<RemoveCv<To>>(val);
}

// Type-safe, constexpr bit reinterpretation (replaces <bit> std::bit_cast and type-punning memcpy).
template <typename To, typename From>
    requires(sizeof(To) == sizeof(From) && IsTriviallyCopyable<To> && IsTriviallyCopyable<From>)
[[nodiscard]] constexpr To bit_cast(const From& src) noexcept {
    return __builtin_bit_cast(To, src);
}

// ============================================================================
// Numeric Limits (replaces <limits> and C macros NAN / HUGE_VAL / INT_MAX)
// ============================================================================

template <typename T> struct Limits;

template <typename T>
    requires(IsIntegral<T> && !IsSame<RemoveCv<T>, bool>)
struct Limits<T> {
    using CleanT = RemoveCv<T>;
    static constexpr usize kBits = sizeof(CleanT) * 8;
    static constexpr CleanT kMin =
        IsSigned<CleanT> ? static_cast<CleanT>(as<u64>(1) << (kBits - 1)) : static_cast<CleanT>(0);
    static constexpr CleanT kLowest = kMin;
    static constexpr CleanT kMax =
        IsSigned<CleanT> ? static_cast<CleanT>(0xFFFFFFFFFFFFFFFFULL >> (65 - kBits))
                         : static_cast<CleanT>(0xFFFFFFFFFFFFFFFFULL >> (64 - kBits));
};

template <> struct Limits<f32> {
    static constexpr f32 kMax = __builtin_bit_cast(f32, 0x7F7FFFFFu);
    static constexpr f32 kLowest = -kMax;
    static constexpr f32 kMinPositive = __builtin_bit_cast(f32, 0x00800000u);
    static constexpr f32 kMin = kMinPositive;
    static constexpr f32 kDenormMin = __builtin_bit_cast(f32, 0x00000001u);
    static constexpr f32 kEpsilon = __builtin_bit_cast(f32, 0x34000000u); // 2^-23
    [[nodiscard]] static constexpr f32 infinity() noexcept { return __builtin_huge_valf(); }
    [[nodiscard]] static constexpr f32 quiet_nan() noexcept { return __builtin_nanf(""); }
};

template <> struct Limits<f64> {
    static constexpr f64 kMax = __builtin_bit_cast(f64, 0x7FEFFFFFFFFFFFFFULL);
    static constexpr f64 kLowest = -kMax;
    static constexpr f64 kMinPositive = __builtin_bit_cast(f64, 0x0010000000000000ULL);
    static constexpr f64 kMin = kMinPositive;
    static constexpr f64 kDenormMin = __builtin_bit_cast(f64, 0x0000000000000001ULL);
    static constexpr f64 kEpsilon = __builtin_bit_cast(f64, 0x3CB0000000000000ULL); // 2^-52
    [[nodiscard]] static constexpr f64 infinity() noexcept { return __builtin_huge_val(); }
    [[nodiscard]] static constexpr f64 quiet_nan() noexcept { return __builtin_nan(""); }
};

// Checked narrowing / sign-changing arithmetic conversion (asserts representability via KEEL_ASSERT).
template <typename To, typename From>
    requires(IsArithmetic<To> && IsArithmetic<From>)
[[nodiscard]] constexpr RemoveCv<To> narrow(From val) noexcept {
    KEEL_ASSERT(detail::in_numeric_range<RemoveCv<To>>(val));
    return static_cast<RemoveCv<To>>(val);
}

// Explicit int -> enum conversion (symmetric with to_underlying; asserts representability in UnderlyingType<E>).
template <typename E, typename From>
    requires(IsEnum<E> && IsIntegral<From> && !IsSame<RemoveCv<From>, bool>)
[[nodiscard]] constexpr RemoveCv<E> from_underlying(From val) noexcept {
    using U = UnderlyingType<E>;
    KEEL_ASSERT(detail::in_numeric_range<U>(val));
    return static_cast<RemoveCv<E>>(static_cast<U>(val));
}

// UB-free saturating arithmetic conversion (clamps out-of-range values to [min, max]; NaN -> 0 for integers).
template <typename To, typename From>
    requires(IsArithmetic<To> && IsArithmetic<From> && !IsSame<RemoveCv<To>, bool>)
[[nodiscard]] constexpr RemoveCv<To> saturate(From val) noexcept {
    using ToT = RemoveCv<To>;
    using FromT = RemoveCv<From>;

    if constexpr (IsIntegral<ToT> && IsIntegral<FromT>) {
        if constexpr (IsSame<FromT, bool>) {
            return static_cast<ToT>(val);
        } else if constexpr (IsSigned<FromT> == IsSigned<ToT>) {
            if constexpr (sizeof(ToT) >= sizeof(FromT)) {
                return static_cast<ToT>(val);
            } else {
                constexpr FromT kLo = static_cast<FromT>(detail::int_min_val<ToT>());
                constexpr FromT kHi = static_cast<FromT>(detail::int_max_val<ToT>());
                if (val < kLo) return detail::int_min_val<ToT>();
                if (val > kHi) return detail::int_max_val<ToT>();
                return static_cast<ToT>(val);
            }
        } else if constexpr (IsSigned<FromT> && !IsSigned<ToT>) {
            if (val <= static_cast<FromT>(0)) {
                return static_cast<ToT>(0);
            }
            constexpr u64 kHi = static_cast<u64>(detail::int_max_val<ToT>());
            if (static_cast<u64>(val) > kHi) {
                return detail::int_max_val<ToT>();
            }
            return static_cast<ToT>(val);
        } else {
            // Unsigned -> Signed
            constexpr u64 kHi = static_cast<u64>(detail::int_max_val<ToT>());
            if (static_cast<u64>(val) > kHi) {
                return detail::int_max_val<ToT>();
            }
            return static_cast<ToT>(val);
        }
    } else if constexpr (IsIntegral<ToT> && IsFloatingPoint<FromT>) {
        if (val != val) {
            return static_cast<ToT>(0); // NaN -> 0
        }
        const f64 d = static_cast<f64>(val);
        if constexpr (sizeof(ToT) == 8) {
            if constexpr (IsSigned<ToT>) {
                if (d <= -9223372036854775808.0) return detail::int_min_val<ToT>();
                if (d >= 9223372036854775808.0) return detail::int_max_val<ToT>();
                return static_cast<ToT>(d);
            } else {
                if (d <= 0.0) return static_cast<ToT>(0);
                if (d >= 18446744073709551616.0) return detail::int_max_val<ToT>();
                return static_cast<ToT>(d);
            }
        } else {
            constexpr f64 kLo = static_cast<f64>(detail::int_min_val<ToT>());
            constexpr f64 kHi = static_cast<f64>(detail::int_max_val<ToT>());
            if (d <= kLo) return detail::int_min_val<ToT>();
            if (d >= kHi) return detail::int_max_val<ToT>();
            return static_cast<ToT>(d);
        }
    } else if constexpr (IsFloatingPoint<ToT> && IsIntegral<FromT>) {
        return static_cast<ToT>(val);
    } else {
        // FloatingPoint -> FloatingPoint
        if constexpr (sizeof(ToT) >= sizeof(FromT)) {
            return static_cast<ToT>(val);
        } else {
            if (val != val) {
                return static_cast<ToT>(val); // NaN -> NaN
            }
            constexpr f64 kF32Max = 3.4028234663852886e+38;
            const f64 d = static_cast<f64>(val);
            if (d > kF32Max) return static_cast<ToT>(kF32Max);
            if (d < -kF32Max) return static_cast<ToT>(-kF32Max);
            return static_cast<ToT>(d);
        }
    }
}

} // namespace keel
