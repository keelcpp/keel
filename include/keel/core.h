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

inline constexpr usize kInvalidIndex = ~usize{0};
inline constexpr usize npos = ~usize{0};

namespace detail {
[[noreturn]] void assert_fail(const char* expr, const char* file, int line) noexcept;
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
inline constexpr bool IsSigned = (IsIntegral<T> || IsFloatingPoint<T>) && !IsSame<RemoveCv<T>, bool> &&
                                 (static_cast<RemoveCv<T>>(-1) < static_cast<RemoveCv<T>>(0));

template <typename T> inline constexpr bool IsUnsigned = IsIntegral<T> && !IsSigned<T>;

template <typename Base, typename Derived> inline constexpr bool IsBaseOf = __is_base_of(Base, Derived);

namespace detail {
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

template <typename E>
    requires IsEnum<E>
using UnderlyingType = typename detail::UnderlyingTypeImpl<RemoveCv<E>>::Type;

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

template <typename T> [[nodiscard]] constexpr T&& forward(RemoveReference<T>& val) noexcept {
    return static_cast<T&&>(val);
}

template <typename T> [[nodiscard]] constexpr T&& forward(RemoveReference<T>&& val) noexcept {
    return static_cast<T&&>(val);
}

template <typename T> constexpr void swap(T& a, T& b) noexcept {
    T tmp = keel::move(a);
    a = keel::move(b);
    b = keel::move(tmp);
}

template <typename T, typename U = T> constexpr T exchange(T& obj, U&& new_val) noexcept {
    T old_val = keel::move(obj);
    obj = keel::forward<U>(new_val);
    return old_val;
}

template <typename T> [[nodiscard]] constexpr const T& min(const T& a, const T& b) noexcept {
    return (b < a) ? b : a;
}

template <typename T> [[nodiscard]] constexpr const T& max(const T& a, const T& b) noexcept {
    return (a < b) ? b : a;
}

template <typename T> [[nodiscard]] constexpr const T& clamp(const T& v, const T& lo, const T& hi) noexcept {
    return (v < lo) ? lo : ((hi < v) ? hi : v);
}

} // namespace keel
