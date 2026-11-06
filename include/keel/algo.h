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
constexpr void insertion_sort_range(T* data, isize count, LessFn& less) noexcept {
    for (isize i = 1; i < count; ++i) {
        T key = keel::move(data[i]);
        isize j = i - 1;
        while (j >= 0 && less(key, data[j])) {
            data[j + 1] = keel::move(data[j]);
            --j;
        }
        data[j + 1] = keel::move(key);
    }
}

} // namespace detail

// Non-recursive median-of-three quicksort with insertion-sort threshold and tail-call elimination.
template <typename T, typename LessFn = DefaultLess>
constexpr void sort(T* data, isize count, LessFn less = LessFn{}) noexcept {
    if (count <= 1) return;

    struct Range {
        isize lo;
        isize hi;
    };
    Range stack[64]{};
    isize sp = 0;
    stack[sp++] = Range{0, count - 1};

    while (sp > 0) {
        const Range cur = stack[--sp];
        isize lo = cur.lo;
        isize hi = cur.hi;

        while (hi - lo >= 16) {
            const isize mid = lo + ((hi - lo) >> 1);
            if (less(data[mid], data[lo])) keel::swap(data[mid], data[lo]);
            if (less(data[hi], data[lo])) keel::swap(data[hi], data[lo]);
            if (less(data[hi], data[mid])) keel::swap(data[hi], data[mid]);

            const T pivot = data[mid];
            isize i = lo;
            isize j = hi;
            while (true) {
                while (less(data[i], pivot)) ++i;
                while (less(pivot, data[j])) --j;
                if (i >= j) break;
                keel::swap(data[i], data[j]);
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
            detail::insertion_sort_range(data + lo, hi - lo + 1, less);
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
    keel::sort(range.data(), static_cast<isize>(range.size()), keel::move(less));
}

template <typename T, typename V, typename LessFn = DefaultLess>
[[nodiscard]] constexpr isize lower_bound(ArrayRef<T> slice, const V& value, LessFn less = LessFn{}) noexcept {
    isize lo = 0;
    isize hi = slice.size();
    while (lo < hi) {
        const isize mid = lo + ((hi - lo) >> 1);
        if (less(slice[mid], value)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

template <typename T, typename V, typename LessFn = DefaultLess>
[[nodiscard]] constexpr isize upper_bound(ArrayRef<T> slice, const V& value, LessFn less = LessFn{}) noexcept {
    isize lo = 0;
    isize hi = slice.size();
    while (lo < hi) {
        const isize mid = lo + ((hi - lo) >> 1);
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
    const isize idx = keel::lower_bound(slice, value, less);
    return idx < slice.size() && !less(value, slice[idx]);
}

template <typename T> constexpr void reverse(MutArrayRef<T> slice) noexcept {
    isize i = 0;
    isize j = slice.size() - 1;
    while (i < j) {
        keel::swap(slice[i], slice[j]);
        ++i;
        --j;
    }
}

template <typename T> constexpr void fill(MutArrayRef<T> slice, const T& value) {
    for (isize i = 0; i < slice.size(); ++i) {
        slice[i] = value;
    }
}

} // namespace keel
