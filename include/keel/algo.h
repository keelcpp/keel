#pragma once

#include "keel/core.h"
#include "keel/ref.h"

namespace keel {

struct DefaultLess {
    template <typename A, typename B>
    [[nodiscard]] constexpr bool operator()(const A& a, const B& b) const noexcept {
        return a < b;
    }
};

namespace detail {

template <typename T, typename LessFn>
constexpr void insertion_sort_range(T* data, usize count, LessFn& less) noexcept {
    for (usize i = 1; i < count; ++i) {
        T key = keel::move(data[i]);
        usize j = i;
        while (j > 0 && less(key, data[j - 1])) {
            data[j] = keel::move(data[j - 1]);
            --j;
        }
        data[j] = keel::move(key);
    }
}

} // namespace detail

// Non-recursive median-of-three quicksort with insertion-sort threshold and tail-call elimination.
template <typename T, typename LessFn = DefaultLess>
constexpr void sort(T* data, usize count, LessFn less = LessFn{}) noexcept {
    if (count <= 1) return;

    struct Range {
        isize lo;
        isize hi;
    };
    Range stack[64]{};
    usize sp = 0;
    stack[sp++] = Range{0, as<isize>(count - 1)};

    while (sp > 0) {
        const Range cur = stack[--sp];
        isize lo = cur.lo;
        isize hi = cur.hi;

        while (hi - lo >= 16) {
            const isize mid = lo + ((hi - lo) >> 1);
            if (less(data[mid], data[lo])) keel::swap(data[mid], data[lo]);
            if (less(data[hi], data[lo])) keel::swap(data[hi], data[lo]);
            if (less(data[hi], data[mid])) keel::swap(data[hi], data[mid]);

            isize pivot_idx = mid;
            isize i = lo;
            isize j = hi;
            while (true) {
                while (less(data[i], data[pivot_idx])) ++i;
                while (less(data[pivot_idx], data[j])) --j;
                if (i >= j) break;
                keel::swap(data[i], data[j]);
                if (pivot_idx == i) {
                    pivot_idx = j;
                } else if (pivot_idx == j) {
                    pivot_idx = i;
                }
                ++i;
                --j;
            }

            if (j - lo < hi - i) {
                if (i < hi && sp < 64) stack[sp++] = Range{i, hi};
                hi = j;
            } else {
                if (lo < j && sp < 64) stack[sp++] = Range{lo, j};
                lo = i;
            }
        }

        if (hi > lo) {
            detail::insertion_sort_range(data + lo, as<usize>(hi - lo + 1), less);
        }
    }
}

template <typename T, typename LessFn = DefaultLess>
constexpr void sort(MutArrayRef<T> slice, LessFn less = LessFn{}) noexcept {
    keel::sort(slice.data(), slice.size(), keel::move(less));
}

template <typename R, typename LessFn = DefaultLess>
    requires requires(R& r) {
        r.data();
        r.size();
    }
constexpr void sort(R& range, LessFn less = LessFn{}) noexcept {
    keel::sort(range.data(), as<usize>(range.size()), keel::move(less));
}

template <typename T, typename V, typename LessFn = DefaultLess>
[[nodiscard]] constexpr usize lower_bound(ArrayRef<T> slice, const V& value, LessFn less = LessFn{}) noexcept {
    usize lo = 0;
    usize hi = slice.size();
    while (lo < hi) {
        const usize mid = lo + ((hi - lo) >> 1);
        if (less(slice[mid], value)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

template <typename T, typename V, typename LessFn = DefaultLess>
[[nodiscard]] constexpr usize upper_bound(ArrayRef<T> slice, const V& value, LessFn less = LessFn{}) noexcept {
    usize lo = 0;
    usize hi = slice.size();
    while (lo < hi) {
        const usize mid = lo + ((hi - lo) >> 1);
        if (!less(value, slice[mid])) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

template <typename T, typename V, typename LessFn = DefaultLess>
[[nodiscard]] constexpr bool binary_search(ArrayRef<T> slice, const V& value, LessFn less = LessFn{}) noexcept {
    const usize idx = keel::lower_bound(slice, value, less);
    return idx < slice.size() && !less(value, slice[idx]);
}

template <typename T> constexpr void reverse(MutArrayRef<T> slice) noexcept {
    if (slice.size() <= 1) return;
    usize i = 0;
    usize j = slice.size() - 1;
    while (i < j) {
        keel::swap(slice[i], slice[j]);
        ++i;
        --j;
    }
}

template <typename T> constexpr void fill(MutArrayRef<T> slice, const T& value) {
    for (usize i = 0; i < slice.size(); ++i) {
        slice[i] = value;
    }
}

template <typename R, typename T>
    requires requires(R& r) {
        r.data();
        r.size();
    }
constexpr void fill(R& range, const T& value) {
    auto* p = range.data();
    const usize n = as<usize>(range.size());
    for (usize i = 0; i < n; ++i) {
        p[i] = value;
    }
}

template <typename T, typename Fn> constexpr void for_each(T* data, usize count, Fn&& fn) {
    for (usize i = 0; i < count; ++i) {
        fn(data[i]);
    }
}

template <typename T, typename Fn> constexpr void for_each(const T* data, usize count, Fn&& fn) {
    for (usize i = 0; i < count; ++i) {
        fn(data[i]);
    }
}

template <typename T, typename Fn> constexpr void for_each(MutArrayRef<T> slice, Fn&& fn) {
    keel::for_each(slice.data(), slice.size(), keel::forward<Fn>(fn));
}

template <typename T, typename Fn> constexpr void for_each(ArrayRef<T> slice, Fn&& fn) {
    keel::for_each(slice.data(), slice.size(), keel::forward<Fn>(fn));
}

template <typename R, typename Fn>
    requires requires(R& r) {
        r.data();
        r.size();
    }
constexpr void for_each(R& range, Fn&& fn) {
    keel::for_each(range.data(), as<usize>(range.size()), keel::forward<Fn>(fn));
}

template <typename R, typename Fn>
    requires requires(const R& r) {
        r.data();
        r.size();
    }
constexpr void for_each(const R& range, Fn&& fn) {
    keel::for_each(range.data(), as<usize>(range.size()), keel::forward<Fn>(fn));
}

template <typename In, typename Out, typename Fn>
constexpr void transform(ArrayRef<In> src, MutArrayRef<Out> dst, Fn&& fn) {
    KEEL_BOUNDS_ASSERT(src.size() <= dst.size());
    const usize n = src.size();
    for (usize i = 0; i < n; ++i) {
        dst[i] = fn(src[i]);
    }
}

template <typename InR, typename OutR, typename Fn>
    requires requires(const InR& a, OutR& b) {
        a.data();
        a.size();
        b.data();
        b.size();
    }
constexpr void transform(const InR& src, OutR& dst, Fn&& fn) {
    KEEL_BOUNDS_ASSERT(as<usize>(src.size()) <= as<usize>(dst.size()));
    const auto* in = src.data();
    auto* out = dst.data();
    const usize n = as<usize>(src.size());
    for (usize i = 0; i < n; ++i) {
        out[i] = fn(in[i]);
    }
}

template <typename T> constexpr void iota(MutArrayRef<T> slice, T start = T{0}) {
    for (usize i = 0; i < slice.size(); ++i) {
        slice[i] = start;
        ++start;
    }
}

template <typename R, typename T = RemoveCvRef<decltype(*declval<R&>().data())>>
    requires requires(R& r) {
        r.data();
        r.size();
    }
constexpr void iota(R& range, T start = T{0}) {
    keel::iota(MutArrayRef<RemoveReference<decltype(*range.data())>>(range.data(), as<usize>(range.size())), start);
}

template <typename T, typename Pred>
[[nodiscard]] constexpr bool all_of(ArrayRef<T> slice, Pred&& pred) {
    for (usize i = 0; i < slice.size(); ++i) {
        if (!pred(slice[i])) return false;
    }
    return true;
}

template <typename R, typename Pred>
    requires requires(const R& r) {
        r.data();
        r.size();
    }
[[nodiscard]] constexpr bool all_of(const R& range, Pred&& pred) {
    const auto* p = range.data();
    const usize n = as<usize>(range.size());
    for (usize i = 0; i < n; ++i) {
        if (!pred(p[i])) return false;
    }
    return true;
}

template <typename T, typename Pred>
[[nodiscard]] constexpr bool any_of(ArrayRef<T> slice, Pred&& pred) {
    for (usize i = 0; i < slice.size(); ++i) {
        if (pred(slice[i])) return true;
    }
    return false;
}

template <typename R, typename Pred>
    requires requires(const R& r) {
        r.data();
        r.size();
    }
[[nodiscard]] constexpr bool any_of(const R& range, Pred&& pred) {
    const auto* p = range.data();
    const usize n = as<usize>(range.size());
    for (usize i = 0; i < n; ++i) {
        if (pred(p[i])) return true;
    }
    return false;
}

template <typename T, typename Pred>
[[nodiscard]] constexpr bool none_of(ArrayRef<T> slice, Pred&& pred) {
    return !keel::any_of(slice, keel::forward<Pred>(pred));
}

template <typename R, typename Pred>
    requires requires(const R& r) {
        r.data();
        r.size();
    }
[[nodiscard]] constexpr bool none_of(const R& range, Pred&& pred) {
    return !keel::any_of(range, keel::forward<Pred>(pred));
}

template <typename T, typename V>
[[nodiscard]] constexpr usize find(ArrayRef<T> slice, const V& value) {
    for (usize i = 0; i < slice.size(); ++i) {
        if (slice[i] == value) return i;
    }
    return kInvalidIndex;
}

template <typename R, typename V>
    requires requires(const R& r) {
        r.data();
        r.size();
    }
[[nodiscard]] constexpr usize find(const R& range, const V& value) {
    const auto* p = range.data();
    const usize n = as<usize>(range.size());
    for (usize i = 0; i < n; ++i) {
        if (p[i] == value) return i;
    }
    return kInvalidIndex;
}

template <typename T, typename Pred>
[[nodiscard]] constexpr usize find_if(ArrayRef<T> slice, Pred&& pred) {
    for (usize i = 0; i < slice.size(); ++i) {
        if (pred(slice[i])) return i;
    }
    return kInvalidIndex;
}

template <typename R, typename Pred>
    requires requires(const R& r) {
        r.data();
        r.size();
    }
[[nodiscard]] constexpr usize find_if(const R& range, Pred&& pred) {
    const auto* p = range.data();
    const usize n = as<usize>(range.size());
    for (usize i = 0; i < n; ++i) {
        if (pred(p[i])) return i;
    }
    return kInvalidIndex;
}

template <typename T, typename V>
[[nodiscard]] constexpr usize count(ArrayRef<T> slice, const V& value) {
    usize n = 0;
    for (usize i = 0; i < slice.size(); ++i) {
        if (slice[i] == value) ++n;
    }
    return n;
}

template <typename T, typename Pred>
[[nodiscard]] constexpr usize count_if(ArrayRef<T> slice, Pred&& pred) {
    usize n = 0;
    for (usize i = 0; i < slice.size(); ++i) {
        if (pred(slice[i])) ++n;
    }
    return n;
}

} // namespace keel
