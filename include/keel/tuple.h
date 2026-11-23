#pragma once

#include "keel/core.h"

namespace keel {

// ============================================================================
// Ignore sentinel (for keel::tie(keel::ignore, x) = ...)
// ============================================================================

namespace detail {
struct Ignore {
    template <typename T> constexpr const Ignore& operator=(const T&) const noexcept { return *this; }
};
} // namespace detail

inline constexpr detail::Ignore ignore{};

// ============================================================================
// Tuple (aggregate specializations for arities 0..12; native C++17/20 structured
// bindings without <tuple> / std::tuple_size)
// ============================================================================

template <typename... Ts> struct Tuple;

template <> struct Tuple<> {
    static constexpr usize kSize = 0;
};

template <typename T0> struct Tuple<T0> {
    static constexpr usize kSize = 1;
    T0 v0;

    template <typename U0> constexpr Tuple& operator=(const Tuple<U0>& o) {
        v0 = o.v0;
        return *this;
    }
    template <typename U0> constexpr Tuple& operator=(Tuple<U0>&& o) {
        v0 = keel::forward<U0>(o.v0);
        return *this;
    }
};

template <typename T0, typename T1> struct Tuple<T0, T1> {
    static constexpr usize kSize = 2;
    T0 v0;
    T1 v1;

    template <typename U0, typename U1> constexpr Tuple& operator=(const Tuple<U0, U1>& o) {
        v0 = o.v0;
        v1 = o.v1;
        return *this;
    }
    template <typename U0, typename U1> constexpr Tuple& operator=(Tuple<U0, U1>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        return *this;
    }
    template <typename U0, typename U1> constexpr Tuple& operator=(const Pair<U0, U1>& o) {
        v0 = o.first;
        v1 = o.second;
        return *this;
    }
    template <typename U0, typename U1> constexpr Tuple& operator=(Pair<U0, U1>&& o) {
        v0 = keel::forward<U0>(o.first);
        v1 = keel::forward<U1>(o.second);
        return *this;
    }
};

template <typename T0, typename T1, typename T2> struct Tuple<T0, T1, T2> {
    static constexpr usize kSize = 3;
    T0 v0;
    T1 v1;
    T2 v2;

    template <typename U0, typename U1, typename U2> constexpr Tuple& operator=(const Tuple<U0, U1, U2>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        return *this;
    }
    template <typename U0, typename U1, typename U2> constexpr Tuple& operator=(Tuple<U0, U1, U2>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        return *this;
    }
};

template <typename T0, typename T1, typename T2, typename T3> struct Tuple<T0, T1, T2, T3> {
    static constexpr usize kSize = 4;
    T0 v0;
    T1 v1;
    T2 v2;
    T3 v3;

    template <typename U0, typename U1, typename U2, typename U3>
    constexpr Tuple& operator=(const Tuple<U0, U1, U2, U3>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        v3 = o.v3;
        return *this;
    }
    template <typename U0, typename U1, typename U2, typename U3>
    constexpr Tuple& operator=(Tuple<U0, U1, U2, U3>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        v3 = keel::forward<U3>(o.v3);
        return *this;
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4> struct Tuple<T0, T1, T2, T3, T4> {
    static constexpr usize kSize = 5;
    T0 v0;
    T1 v1;
    T2 v2;
    T3 v3;
    T4 v4;

    template <typename U0, typename U1, typename U2, typename U3, typename U4>
    constexpr Tuple& operator=(const Tuple<U0, U1, U2, U3, U4>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        v3 = o.v3;
        v4 = o.v4;
        return *this;
    }
    template <typename U0, typename U1, typename U2, typename U3, typename U4>
    constexpr Tuple& operator=(Tuple<U0, U1, U2, U3, U4>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        v3 = keel::forward<U3>(o.v3);
        v4 = keel::forward<U4>(o.v4);
        return *this;
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5>
struct Tuple<T0, T1, T2, T3, T4, T5> {
    static constexpr usize kSize = 6;
    T0 v0;
    T1 v1;
    T2 v2;
    T3 v3;
    T4 v4;
    T5 v5;

    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5>
    constexpr Tuple& operator=(const Tuple<U0, U1, U2, U3, U4, U5>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        v3 = o.v3;
        v4 = o.v4;
        v5 = o.v5;
        return *this;
    }
    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5>
    constexpr Tuple& operator=(Tuple<U0, U1, U2, U3, U4, U5>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        v3 = keel::forward<U3>(o.v3);
        v4 = keel::forward<U4>(o.v4);
        v5 = keel::forward<U5>(o.v5);
        return *this;
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
struct Tuple<T0, T1, T2, T3, T4, T5, T6> {
    static constexpr usize kSize = 7;
    T0 v0;
    T1 v1;
    T2 v2;
    T3 v3;
    T4 v4;
    T5 v5;
    T6 v6;

    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6>
    constexpr Tuple& operator=(const Tuple<U0, U1, U2, U3, U4, U5, U6>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        v3 = o.v3;
        v4 = o.v4;
        v5 = o.v5;
        v6 = o.v6;
        return *this;
    }
    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6>
    constexpr Tuple& operator=(Tuple<U0, U1, U2, U3, U4, U5, U6>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        v3 = keel::forward<U3>(o.v3);
        v4 = keel::forward<U4>(o.v4);
        v5 = keel::forward<U5>(o.v5);
        v6 = keel::forward<U6>(o.v6);
        return *this;
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
struct Tuple<T0, T1, T2, T3, T4, T5, T6, T7> {
    static constexpr usize kSize = 8;
    T0 v0;
    T1 v1;
    T2 v2;
    T3 v3;
    T4 v4;
    T5 v5;
    T6 v6;
    T7 v7;

    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7>
    constexpr Tuple& operator=(const Tuple<U0, U1, U2, U3, U4, U5, U6, U7>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        v3 = o.v3;
        v4 = o.v4;
        v5 = o.v5;
        v6 = o.v6;
        v7 = o.v7;
        return *this;
    }
    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7>
    constexpr Tuple& operator=(Tuple<U0, U1, U2, U3, U4, U5, U6, U7>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        v3 = keel::forward<U3>(o.v3);
        v4 = keel::forward<U4>(o.v4);
        v5 = keel::forward<U5>(o.v5);
        v6 = keel::forward<U6>(o.v6);
        v7 = keel::forward<U7>(o.v7);
        return *this;
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7,
          typename T8>
struct Tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8> {
    static constexpr usize kSize = 9;
    T0 v0;
    T1 v1;
    T2 v2;
    T3 v3;
    T4 v4;
    T5 v5;
    T6 v6;
    T7 v7;
    T8 v8;

    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7,
              typename U8>
    constexpr Tuple& operator=(const Tuple<U0, U1, U2, U3, U4, U5, U6, U7, U8>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        v3 = o.v3;
        v4 = o.v4;
        v5 = o.v5;
        v6 = o.v6;
        v7 = o.v7;
        v8 = o.v8;
        return *this;
    }
    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7,
              typename U8>
    constexpr Tuple& operator=(Tuple<U0, U1, U2, U3, U4, U5, U6, U7, U8>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        v3 = keel::forward<U3>(o.v3);
        v4 = keel::forward<U4>(o.v4);
        v5 = keel::forward<U5>(o.v5);
        v6 = keel::forward<U6>(o.v6);
        v7 = keel::forward<U7>(o.v7);
        v8 = keel::forward<U8>(o.v8);
        return *this;
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7,
          typename T8, typename T9>
struct Tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9> {
    static constexpr usize kSize = 10;
    T0 v0;
    T1 v1;
    T2 v2;
    T3 v3;
    T4 v4;
    T5 v5;
    T6 v6;
    T7 v7;
    T8 v8;
    T9 v9;

    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7,
              typename U8, typename U9>
    constexpr Tuple& operator=(const Tuple<U0, U1, U2, U3, U4, U5, U6, U7, U8, U9>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        v3 = o.v3;
        v4 = o.v4;
        v5 = o.v5;
        v6 = o.v6;
        v7 = o.v7;
        v8 = o.v8;
        v9 = o.v9;
        return *this;
    }
    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7,
              typename U8, typename U9>
    constexpr Tuple& operator=(Tuple<U0, U1, U2, U3, U4, U5, U6, U7, U8, U9>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        v3 = keel::forward<U3>(o.v3);
        v4 = keel::forward<U4>(o.v4);
        v5 = keel::forward<U5>(o.v5);
        v6 = keel::forward<U6>(o.v6);
        v7 = keel::forward<U7>(o.v7);
        v8 = keel::forward<U8>(o.v8);
        v9 = keel::forward<U9>(o.v9);
        return *this;
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7,
          typename T8, typename T9, typename T10>
struct Tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10> {
    static constexpr usize kSize = 11;
    T0 v0;
    T1 v1;
    T2 v2;
    T3 v3;
    T4 v4;
    T5 v5;
    T6 v6;
    T7 v7;
    T8 v8;
    T9 v9;
    T10 v10;

    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7,
              typename U8, typename U9, typename U10>
    constexpr Tuple& operator=(const Tuple<U0, U1, U2, U3, U4, U5, U6, U7, U8, U9, U10>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        v3 = o.v3;
        v4 = o.v4;
        v5 = o.v5;
        v6 = o.v6;
        v7 = o.v7;
        v8 = o.v8;
        v9 = o.v9;
        v10 = o.v10;
        return *this;
    }
    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7,
              typename U8, typename U9, typename U10>
    constexpr Tuple& operator=(Tuple<U0, U1, U2, U3, U4, U5, U6, U7, U8, U9, U10>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        v3 = keel::forward<U3>(o.v3);
        v4 = keel::forward<U4>(o.v4);
        v5 = keel::forward<U5>(o.v5);
        v6 = keel::forward<U6>(o.v6);
        v7 = keel::forward<U7>(o.v7);
        v8 = keel::forward<U8>(o.v8);
        v9 = keel::forward<U9>(o.v9);
        v10 = keel::forward<U10>(o.v10);
        return *this;
    }
};

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7,
          typename T8, typename T9, typename T10, typename T11>
struct Tuple<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11> {
    static constexpr usize kSize = 12;
    T0 v0;
    T1 v1;
    T2 v2;
    T3 v3;
    T4 v4;
    T5 v5;
    T6 v6;
    T7 v7;
    T8 v8;
    T9 v9;
    T10 v10;
    T11 v11;

    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7,
              typename U8, typename U9, typename U10, typename U11>
    constexpr Tuple& operator=(const Tuple<U0, U1, U2, U3, U4, U5, U6, U7, U8, U9, U10, U11>& o) {
        v0 = o.v0;
        v1 = o.v1;
        v2 = o.v2;
        v3 = o.v3;
        v4 = o.v4;
        v5 = o.v5;
        v6 = o.v6;
        v7 = o.v7;
        v8 = o.v8;
        v9 = o.v9;
        v10 = o.v10;
        v11 = o.v11;
        return *this;
    }
    template <typename U0, typename U1, typename U2, typename U3, typename U4, typename U5, typename U6, typename U7,
              typename U8, typename U9, typename U10, typename U11>
    constexpr Tuple& operator=(Tuple<U0, U1, U2, U3, U4, U5, U6, U7, U8, U9, U10, U11>&& o) {
        v0 = keel::forward<U0>(o.v0);
        v1 = keel::forward<U1>(o.v1);
        v2 = keel::forward<U2>(o.v2);
        v3 = keel::forward<U3>(o.v3);
        v4 = keel::forward<U4>(o.v4);
        v5 = keel::forward<U5>(o.v5);
        v6 = keel::forward<U6>(o.v6);
        v7 = keel::forward<U7>(o.v7);
        v8 = keel::forward<U8>(o.v8);
        v9 = keel::forward<U9>(o.v9);
        v10 = keel::forward<U10>(o.v10);
        v11 = keel::forward<U11>(o.v11);
        return *this;
    }
};

template <typename... Ts> Tuple(Ts...) -> Tuple<Ts...>;

// ============================================================================
// TupleSize / TupleElement traits
// ============================================================================

namespace detail {
template <typename T> struct TupleSizeImpl;
template <typename... Ts> struct TupleSizeImpl<Tuple<Ts...>> {
    static constexpr usize value = sizeof...(Ts);
};
template <typename A, typename B> struct TupleSizeImpl<Pair<A, B>> {
    static constexpr usize value = 2;
};

template <usize I, typename... Ts> struct NthType;
template <typename Head, typename... Tail> struct NthType<0, Head, Tail...> {
    using Type = Head;
};
template <usize I, typename Head, typename... Tail> struct NthType<I, Head, Tail...> {
    using Type = typename NthType<I - 1, Tail...>::Type;
};

template <usize I, typename T> struct TupleElementImpl;
template <usize I, typename... Ts> struct TupleElementImpl<I, Tuple<Ts...>> {
    using Type = typename NthType<I, Ts...>::Type;
};
template <typename A, typename B> struct TupleElementImpl<0, Pair<A, B>> {
    using Type = A;
};
template <typename A, typename B> struct TupleElementImpl<1, Pair<A, B>> {
    using Type = B;
};
} // namespace detail

template <typename T> inline constexpr usize TupleSize = detail::TupleSizeImpl<RemoveCvRef<T>>::value;
template <usize I, typename T> using TupleElement = typename detail::TupleElementImpl<I, RemoveCvRef<T>>::Type;

// ============================================================================
// keel::get<I> for Tuple and Pair
// ============================================================================

namespace detail {
template <usize I, typename Tup> constexpr decltype(auto) tuple_get_member(Tup& t) noexcept {
    static_assert(I < TupleSize<Tup>, "keel::get index out of bounds");
    if constexpr (I == 0) return (t.v0);
    else if constexpr (I == 1) return (t.v1);
    else if constexpr (I == 2) return (t.v2);
    else if constexpr (I == 3) return (t.v3);
    else if constexpr (I == 4) return (t.v4);
    else if constexpr (I == 5) return (t.v5);
    else if constexpr (I == 6) return (t.v6);
    else if constexpr (I == 7) return (t.v7);
    else if constexpr (I == 8) return (t.v8);
    else if constexpr (I == 9) return (t.v9);
    else if constexpr (I == 10) return (t.v10);
    else if constexpr (I == 11) return (t.v11);
}
} // namespace detail

template <usize I, typename... Ts>
[[nodiscard]] constexpr TupleElement<I, Tuple<Ts...>>& get(Tuple<Ts...>& t) noexcept {
    return detail::tuple_get_member<I>(t);
}

template <usize I, typename... Ts>
[[nodiscard]] constexpr const TupleElement<I, Tuple<Ts...>>& get(const Tuple<Ts...>& t) noexcept {
    return detail::tuple_get_member<I>(t);
}

template <usize I, typename... Ts>
[[nodiscard]] constexpr TupleElement<I, Tuple<Ts...>>&& get(Tuple<Ts...>&& t) noexcept {
    return static_cast<TupleElement<I, Tuple<Ts...>>&&>(detail::tuple_get_member<I>(t));
}

template <usize I, typename... Ts>
[[nodiscard]] constexpr const TupleElement<I, Tuple<Ts...>>&& get(const Tuple<Ts...>&& t) noexcept {
    return static_cast<const TupleElement<I, Tuple<Ts...>>&&>(detail::tuple_get_member<I>(t));
}

template <usize I, typename A, typename B>
[[nodiscard]] constexpr TupleElement<I, Pair<A, B>>& get(Pair<A, B>& p) noexcept {
    static_assert(I < 2, "keel::get index out of bounds for Pair");
    if constexpr (I == 0) return p.first;
    else return p.second;
}

template <usize I, typename A, typename B>
[[nodiscard]] constexpr const TupleElement<I, Pair<A, B>>& get(const Pair<A, B>& p) noexcept {
    static_assert(I < 2, "keel::get index out of bounds for Pair");
    if constexpr (I == 0) return p.first;
    else return p.second;
}

template <usize I, typename A, typename B>
[[nodiscard]] constexpr TupleElement<I, Pair<A, B>>&& get(Pair<A, B>&& p) noexcept {
    static_assert(I < 2, "keel::get index out of bounds for Pair");
    if constexpr (I == 0) return static_cast<A&&>(p.first);
    else return static_cast<B&&>(p.second);
}

template <usize I, typename A, typename B>
[[nodiscard]] constexpr const TupleElement<I, Pair<A, B>>&& get(const Pair<A, B>&& p) noexcept {
    static_assert(I < 2, "keel::get index out of bounds for Pair");
    if constexpr (I == 0) return static_cast<const A&&>(p.first);
    else return static_cast<const B&&>(p.second);
}

// ============================================================================
// Factory helpers: make_tuple, tie, forward_as_tuple
// ============================================================================

template <typename... Ts>
[[nodiscard]] constexpr Tuple<Decay<Ts>...> make_tuple(Ts&&... args) {
    return Tuple<Decay<Ts>...>{keel::forward<Ts>(args)...};
}

template <typename... Ts>
[[nodiscard]] constexpr Tuple<Ts&...> tie(Ts&... args) noexcept {
    return Tuple<Ts&...>{args...};
}

template <typename... Ts>
[[nodiscard]] constexpr Tuple<Ts&&...> forward_as_tuple(Ts&&... args) noexcept {
    return Tuple<Ts&&...>{keel::forward<Ts>(args)...};
}

// ============================================================================
// Relational operators
// ============================================================================

namespace detail {
template <usize I, typename... Ts, typename... Us>
constexpr bool tuple_eq(const Tuple<Ts...>& a, const Tuple<Us...>& b) {
    if constexpr (I == sizeof...(Ts)) {
        return true;
    } else {
        return (keel::get<I>(a) == keel::get<I>(b)) && tuple_eq<I + 1>(a, b);
    }
}

template <usize I, typename... Ts, typename... Us>
constexpr bool tuple_lt(const Tuple<Ts...>& a, const Tuple<Us...>& b) {
    if constexpr (I == sizeof...(Ts)) {
        return false;
    } else {
        if (keel::get<I>(a) < keel::get<I>(b)) return true;
        if (keel::get<I>(b) < keel::get<I>(a)) return false;
        return tuple_lt<I + 1>(a, b);
    }
}
} // namespace detail

template <typename... Ts, typename... Us>
    requires(sizeof...(Ts) == sizeof...(Us))
[[nodiscard]] constexpr bool operator==(const Tuple<Ts...>& a, const Tuple<Us...>& b) {
    return detail::tuple_eq<0>(a, b);
}

template <typename... Ts, typename... Us>
    requires(sizeof...(Ts) == sizeof...(Us))
[[nodiscard]] constexpr bool operator!=(const Tuple<Ts...>& a, const Tuple<Us...>& b) {
    return !(a == b);
}

template <typename... Ts, typename... Us>
    requires(sizeof...(Ts) == sizeof...(Us))
[[nodiscard]] constexpr bool operator<(const Tuple<Ts...>& a, const Tuple<Us...>& b) {
    return detail::tuple_lt<0>(a, b);
}

template <typename... Ts, typename... Us>
    requires(sizeof...(Ts) == sizeof...(Us))
[[nodiscard]] constexpr bool operator<=(const Tuple<Ts...>& a, const Tuple<Us...>& b) {
    return !(b < a);
}

template <typename... Ts, typename... Us>
    requires(sizeof...(Ts) == sizeof...(Us))
[[nodiscard]] constexpr bool operator>(const Tuple<Ts...>& a, const Tuple<Us...>& b) {
    return b < a;
}

template <typename... Ts, typename... Us>
    requires(sizeof...(Ts) == sizeof...(Us))
[[nodiscard]] constexpr bool operator>=(const Tuple<Ts...>& a, const Tuple<Us...>& b) {
    return !(a < b);
}

} // namespace keel
