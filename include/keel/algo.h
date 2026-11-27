#pragma once

#include "keel/core.h"
#include "keel/memory.h"
#include "keel/ref.h"

namespace keel {

struct DefaultLess {
    template <typename A, typename B>
    [[nodiscard]] constexpr bool operator()(const A& a, const B& b) const noexcept {
        return a < b;
    }
};

struct DefaultEqual {
    template <typename A, typename B>
    [[nodiscard]] constexpr bool operator()(const A& a, const B& b) const noexcept {
        return a == b;
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

template <typename R, typename T = RemoveCvRef<decltype(*detail::declval<R&>().data())>>
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

// ============================================================================
// Type-Safe Bulk Memory Operations (replaces raw std::memcpy / memmove / memset)
// ============================================================================

template <typename T>
constexpr void copy_n(T* dst, const T* src, usize count) noexcept(IsTriviallyCopyable<T>) {
    if (count == 0) return;
    if constexpr (IsTriviallyCopyable<T>) {
        if (!__builtin_is_constant_evaluated()) {
            mem_copy(dst, src, count * sizeof(T));
            return;
        }
    }
    for (usize i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

template <typename T>
constexpr void copy(MutArrayRef<T> dst, ArrayRef<T> src) noexcept(IsTriviallyCopyable<T>) {
    KEEL_BOUNDS_ASSERT(dst.size() >= src.size());
    keel::copy_n(dst.data(), src.data(), src.size());
}

template <typename DstR, typename SrcR>
    requires requires(DstR& d, const SrcR& s) {
        d.data();
        d.size();
        s.data();
        s.size();
    }
constexpr void copy(DstR& dst, const SrcR& src) {
    KEEL_BOUNDS_ASSERT(as<usize>(dst.size()) >= as<usize>(src.size()));
    keel::copy_n(dst.data(), src.data(), as<usize>(src.size()));
}

template <typename T>
constexpr void move_n(T* dst, const T* src, usize count) noexcept(IsTriviallyCopyable<T>) {
    if (count == 0 || dst == src) return;
    if constexpr (IsTriviallyCopyable<T>) {
        if (!__builtin_is_constant_evaluated()) {
            mem_move(dst, src, count * sizeof(T));
            return;
        }
    }
    if (dst < src) {
        for (usize i = 0; i < count; ++i) {
            dst[i] = keel::move(const_cast<T&>(src[i]));
        }
    } else {
        for (usize i = count; i > 0; --i) {
            dst[i - 1] = keel::move(const_cast<T&>(src[i - 1]));
        }
    }
}

template <typename T>
constexpr void zero_n(T* dst, usize count) noexcept(IsTriviallyCopyable<T>) {
    if (count == 0) return;
    if constexpr (IsTriviallyCopyable<T>) {
        if (!__builtin_is_constant_evaluated()) {
            mem_zero(dst, count * sizeof(T));
            return;
        }
    }
    for (usize i = 0; i < count; ++i) {
        dst[i] = T{};
    }
}

template <typename T>
constexpr void zero(MutArrayRef<T> dst) noexcept(IsTriviallyCopyable<T>) {
    keel::zero_n(dst.data(), dst.size());
}

template <typename R>
    requires requires(R& r) {
        r.data();
        r.size();
    }
constexpr void zero(R& range) {
    keel::zero_n(range.data(), as<usize>(range.size()));
}

template <typename T>
[[nodiscard]] constexpr bool equal_n(const T* a, const T* b, usize count) noexcept {
    if (count == 0 || a == b) return true;
    for (usize i = 0; i < count; ++i) {
        if (!(a[i] == b[i])) return false;
    }
    return true;
}

// ============================================================================
// Contiguous Pointer-Range Overloads & Extended Algorithms
// (replaces <algorithm> and <numeric>)
// ============================================================================

template <typename T, typename LessFn = DefaultLess>
constexpr void sort(T* first, T* last, LessFn less = LessFn{}) noexcept {
    if (first >= last) return;
    keel::sort(first, as<usize>(last - first), keel::move(less));
}

template <typename T> constexpr void reverse(T* first, T* last) noexcept {
    if (first >= last) return;
    keel::reverse(MutArrayRef<T>(first, as<usize>(last - first)));
}

template <typename R>
    requires requires(R& r) {
        r.data();
        r.size();
    }
constexpr void reverse(R& range) noexcept {
    keel::reverse(range.data(), range.data() + as<usize>(range.size()));
}

template <typename T> constexpr T* rotate(T* first, T* middle, T* last) noexcept {
    if (first == middle) return last;
    if (middle == last) return first;
    keel::reverse(first, middle);
    keel::reverse(middle, last);
    keel::reverse(first, last);
    return first + (last - middle);
}

template <typename T, typename V> constexpr void fill(T* first, T* last, const V& value) {
    for (T* it = first; it < last; ++it) {
        *it = value;
    }
}

template <typename T, typename V> constexpr T* fill_n(T* dst, usize count, const V& value) {
    for (usize i = 0; i < count; ++i) {
        dst[i] = value;
    }
    return dst + count;
}

template <typename T, typename V> constexpr void iota(T* first, T* last, V start) {
    for (T* it = first; it < last; ++it) {
        *it = static_cast<T>(start);
        ++start;
    }
}

template <typename SrcPtr, typename DstPtr>
    requires(IsPointer<SrcPtr> && IsPointer<DstPtr>)
constexpr DstPtr copy(SrcPtr first, SrcPtr last, DstPtr dst) {
    if (first >= last) return dst;
    const usize count = as<usize>(last - first);
    using SrcVal = RemoveCv<RemovePointer<SrcPtr>>;
    using DstVal = RemoveCv<RemovePointer<DstPtr>>;
    if constexpr (IsSame<SrcVal, DstVal> && IsTriviallyCopyable<DstVal>) {
        if (!__builtin_is_constant_evaluated()) {
            mem_copy(dst, first, count * sizeof(DstVal));
            return dst + count;
        }
    }
    for (usize i = 0; i < count; ++i) {
        dst[i] = first[i];
    }
    return dst + count;
}

template <typename SrcPtr1, typename SrcPtr2>
    requires(IsPointer<SrcPtr1> && IsPointer<SrcPtr2>)
[[nodiscard]] constexpr bool equal(SrcPtr1 first1, SrcPtr1 last1, SrcPtr2 first2) {
    for (; first1 < last1; ++first1, ++first2) {
        if (!(*first1 == *first2)) return false;
    }
    return true;
}

template <typename T, typename V, typename LessFn = DefaultLess>
[[nodiscard]] constexpr T* lower_bound(T* first, T* last, const V& value, LessFn less = LessFn{}) {
    if (first >= last) return last;
    const usize n = as<usize>(last - first);
    usize lo = 0;
    usize hi = n;
    while (lo < hi) {
        const usize mid = lo + ((hi - lo) >> 1);
        if (less(first[mid], value)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return first + lo;
}

template <typename T, typename V, typename LessFn = DefaultLess>
[[nodiscard]] constexpr T* upper_bound(T* first, T* last, const V& value, LessFn less = LessFn{}) {
    if (first >= last) return last;
    const usize n = as<usize>(last - first);
    usize lo = 0;
    usize hi = n;
    while (lo < hi) {
        const usize mid = lo + ((hi - lo) >> 1);
        if (!less(value, first[mid])) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return first + lo;
}

template <typename T, typename V, typename LessFn = DefaultLess>
[[nodiscard]] constexpr bool binary_search(const T* first, const T* last, const V& value, LessFn less = LessFn{}) {
    const T* it = keel::lower_bound(first, last, value, less);
    return it != last && !less(value, *it);
}

template <typename T, typename V> [[nodiscard]] constexpr T* find(T* first, T* last, const V& value) {
    for (T* it = first; it < last; ++it) {
        if (*it == value) return it;
    }
    return last;
}

template <typename T, typename Pred> [[nodiscard]] constexpr T* find_if(T* first, T* last, Pred&& pred) {
    for (T* it = first; it < last; ++it) {
        if (pred(*it)) return it;
    }
    return last;
}

template <typename T, typename V> [[nodiscard]] constexpr usize count(const T* first, const T* last, const V& value) {
    usize n = 0;
    for (const T* it = first; it < last; ++it) {
        if (*it == value) ++n;
    }
    return n;
}

template <typename T, typename Pred>
[[nodiscard]] constexpr usize count_if(const T* first, const T* last, Pred&& pred) {
    usize n = 0;
    for (const T* it = first; it < last; ++it) {
        if (pred(*it)) ++n;
    }
    return n;
}

template <typename T, typename LessFn = DefaultLess>
[[nodiscard]] constexpr T* min_element(T* first, T* last, LessFn less = LessFn{}) {
    if (first >= last) return last;
    T* best = first;
    for (T* it = first + 1; it < last; ++it) {
        if (less(*it, *best)) best = it;
    }
    return best;
}

template <typename T, typename LessFn = DefaultLess>
[[nodiscard]] constexpr T* max_element(T* first, T* last, LessFn less = LessFn{}) {
    if (first >= last) return last;
    T* best = first;
    for (T* it = first + 1; it < last; ++it) {
        if (less(*best, *it)) best = it;
    }
    return best;
}

template <typename T, typename EqFn = DefaultEqual>
[[nodiscard]] constexpr T* unique(T* first, T* last, EqFn eq = EqFn{}) {
    if (first >= last) return last;
    T* dst = first;
    for (T* it = first + 1; it < last; ++it) {
        if (!eq(*dst, *it)) {
            ++dst;
            if (dst != it) *dst = keel::move(*it);
        }
    }
    return dst + 1;
}

template <typename T, typename V> [[nodiscard]] constexpr T* remove(T* first, T* last, const V& value) {
    T* dst = first;
    for (T* it = first; it < last; ++it) {
        if (!(*it == value)) {
            if (dst != it) *dst = keel::move(*it);
            ++dst;
        }
    }
    return dst;
}

template <typename T, typename Pred> [[nodiscard]] constexpr T* remove_if(T* first, T* last, Pred&& pred) {
    T* dst = first;
    for (T* it = first; it < last; ++it) {
        if (!pred(*it)) {
            if (dst != it) *dst = keel::move(*it);
            ++dst;
        }
    }
    return dst;
}

template <typename T, typename V1, typename V2>
constexpr void replace(T* first, T* last, const V1& old_val, const V2& new_val) {
    for (T* it = first; it < last; ++it) {
        if (*it == old_val) *it = new_val;
    }
}

// ============================================================================
// Heap Operations & Partial Sort / Nth Element
// ============================================================================

namespace detail {

template <typename T, typename LessFn>
constexpr void sift_down(T* first, usize count, usize root, LessFn& less) {
    T val = keel::move(first[root]);
    while (true) {
        usize child = (root << 1) + 1;
        if (child >= count) break;
        if (child + 1 < count && less(first[child], first[child + 1])) {
            ++child;
        }
        if (!less(val, first[child])) break;
        first[root] = keel::move(first[child]);
        root = child;
    }
    first[root] = keel::move(val);
}

} // namespace detail

template <typename T, typename LessFn = DefaultLess>
constexpr void push_heap(T* first, T* last, LessFn less = LessFn{}) {
    if (last - first <= 1) return;
    usize idx = as<usize>(last - first) - 1;
    T val = keel::move(first[idx]);
    while (idx > 0) {
        const usize parent = (idx - 1) >> 1;
        if (!less(first[parent], val)) break;
        first[idx] = keel::move(first[parent]);
        idx = parent;
    }
    first[idx] = keel::move(val);
}

template <typename T, typename LessFn = DefaultLess>
constexpr void pop_heap(T* first, T* last, LessFn less = LessFn{}) {
    const isize diff = last - first;
    if (diff <= 1) return;
    const usize count = as<usize>(diff) - 1;
    keel::swap(first[0], first[count]);
    detail::sift_down(first, count, 0, less);
}

template <typename T, typename LessFn = DefaultLess>
constexpr void make_heap(T* first, T* last, LessFn less = LessFn{}) {
    const isize diff = last - first;
    if (diff <= 1) return;
    const usize count = as<usize>(diff);
    for (usize i = count / 2; i > 0; --i) {
        detail::sift_down(first, count, i - 1, less);
    }
}

template <typename T, typename LessFn = DefaultLess>
constexpr void sort_heap(T* first, T* last, LessFn less = LessFn{}) {
    while (last - first > 1) {
        keel::pop_heap(first, last, less);
        --last;
    }
}

template <typename T, typename LessFn = DefaultLess>
constexpr void partial_sort(T* first, T* middle, T* last, LessFn less = LessFn{}) {
    if (first >= middle || middle > last) return;
    const usize k = as<usize>(middle - first);
    keel::make_heap(first, middle, less);
    for (T* it = middle; it < last; ++it) {
        if (less(*it, *first)) {
            keel::swap(*it, *first);
            detail::sift_down(first, k, 0, less);
        }
    }
    keel::sort_heap(first, middle, less);
}

template <typename T, typename LessFn = DefaultLess>
constexpr void nth_element(T* first, T* nth, T* last, LessFn less = LessFn{}) {
    if (first >= last || nth < first || nth >= last) return;
    T* lo = first;
    T* hi = last - 1;
    while (hi - lo >= 16) {
        T* mid = lo + ((hi - lo) >> 1);
        if (less(*mid, *lo)) keel::swap(*mid, *lo);
        if (less(*hi, *lo)) keel::swap(*hi, *lo);
        if (less(*hi, *mid)) keel::swap(*hi, *mid);

        T* pivot = mid;
        T* i = lo;
        T* j = hi;
        while (true) {
            while (less(*i, *pivot)) ++i;
            while (less(*pivot, *j)) --j;
            if (i >= j) break;
            keel::swap(*i, *j);
            if (pivot == i) {
                pivot = j;
            } else if (pivot == j) {
                pivot = i;
            }
            ++i;
            --j;
        }

        if (nth <= j) {
            hi = j;
        } else if (nth >= i) {
            lo = i;
        } else {
            return;
        }
    }
    if (hi > lo) {
        detail::insertion_sort_range(lo, as<usize>(hi - lo + 1), less);
    }
}

// ============================================================================
// Stable Sort (O(n log n) top-down merge sort with single scratch buffer and
// O(n log^2 n) in-place rotation fallback for constexpr / OOM)
// ============================================================================

namespace detail {

template <typename T, typename LessFn>
constexpr void inplace_merge_range(T* first, T* mid, T* last, LessFn& less) {
    if (first == mid || mid == last) return;
    if (!less(*mid, *(mid - 1))) return;
    const usize left_len = as<usize>(mid - first);
    const usize right_len = as<usize>(last - mid);
    if (left_len + right_len <= 16) {
        insertion_sort_range(first, left_len + right_len, less);
        return;
    }
    T* first_cut = first;
    T* second_cut = mid;
    if (left_len >= right_len) {
        first_cut = first + (left_len >> 1);
        second_cut = keel::lower_bound(mid, last, *first_cut, less);
    } else {
        second_cut = mid + (right_len >> 1);
        first_cut = keel::upper_bound(first, mid, *second_cut, less);
    }
    T* new_mid = keel::rotate(first_cut, mid, second_cut);
    inplace_merge_range(first, first_cut, new_mid, less);
    inplace_merge_range(new_mid, second_cut, last, less);
}

template <typename T, typename LessFn>
void buffered_merge_range(T* first, T* mid, T* last, T* buf, LessFn& less) {
    if (!less(*mid, *(mid - 1))) return;
    const usize left_len = as<usize>(mid - first);
    struct DestroyGuard {
        T* ptr;
        usize constructed = 0;
        ~DestroyGuard() {
            if constexpr (!IsTriviallyDestructible<T>) {
                for (usize k = constructed; k > 0; --k) {
                    ptr[k - 1].~T();
                }
            }
        }
    } guard{buf, 0};

    for (usize i = 0; i < left_len; ++i) {
        ::new (static_cast<void*>(buf + i)) T(keel::move(first[i]));
        guard.constructed = i + 1;
    }

    T* l = buf;
    T* l_end = buf + left_len;
    T* r = mid;
    T* out = first;
    while (l < l_end && r < last) {
        if (less(*r, *l)) {
            *out = keel::move(*r);
            ++r;
        } else {
            *out = keel::move(*l);
            ++l;
        }
        ++out;
    }
    while (l < l_end) {
        *out = keel::move(*l);
        ++l;
        ++out;
    }
}

template <typename T, typename LessFn>
constexpr void stable_sort_impl(T* first, T* last, T* buf, LessFn& less) {
    const usize count = as<usize>(last - first);
    if (count <= 16) {
        if (count > 1) insertion_sort_range(first, count, less);
        return;
    }
    T* mid = first + (count >> 1);
    stable_sort_impl(first, mid, buf, less);
    stable_sort_impl(mid, last, buf, less);
    if (!__builtin_is_constant_evaluated() && buf != nullptr) {
        buffered_merge_range(first, mid, last, buf, less);
    } else {
        inplace_merge_range(first, mid, last, less);
    }
}

template <typename T, typename LessFn>
inline void stable_sort_runtime(T* data, usize count, LessFn& less) {
    const usize half = count >> 1;
    Allocator* alloc = default_allocator();
    T* buf = alloc ? alloc->try_alloc_array<T>(half) : nullptr;
    struct FreeGuard {
        Allocator* a;
        T* p;
        usize n;
        ~FreeGuard() {
            if (a && p) a->free_array<T>(p, n);
        }
    } free_guard{alloc, buf, half};
    stable_sort_impl(data, data + count, buf, less);
}

} // namespace detail

template <typename T, typename LessFn = DefaultLess>
constexpr void stable_sort(T* data, usize count, LessFn less = LessFn{}) {
    if (count <= 1) return;
    if (count <= 16) {
        detail::insertion_sort_range(data, count, less);
        return;
    }
    if (!__builtin_is_constant_evaluated()) {
        detail::stable_sort_runtime(data, count, less);
    } else {
        detail::stable_sort_impl(data, data + count, static_cast<T*>(nullptr), less);
    }
}

template <typename T, typename LessFn = DefaultLess>
constexpr void stable_sort(T* first, T* last, LessFn less = LessFn{}) {
    if (first >= last) return;
    keel::stable_sort(first, as<usize>(last - first), keel::move(less));
}

template <typename T, typename LessFn = DefaultLess>
constexpr void stable_sort(MutArrayRef<T> slice, LessFn less = LessFn{}) {
    keel::stable_sort(slice.data(), slice.size(), keel::move(less));
}

template <typename R, typename LessFn = DefaultLess>
    requires requires(R& r) {
        r.data();
        r.size();
    }
constexpr void stable_sort(R& range, LessFn less = LessFn{}) {
    keel::stable_sort(range.data(), as<usize>(range.size()), keel::move(less));
}

// ============================================================================
// Permutations
// ============================================================================

template <typename T, typename LessFn = DefaultLess>
constexpr bool next_permutation(T* first, T* last, LessFn less = LessFn{}) {
    if (last - first <= 1) return false;
    T* i = last - 1;
    while (true) {
        T* ii = i;
        --i;
        if (less(*i, *ii)) {
            T* j = last;
            while (!less(*i, *(--j))) {
            }
            keel::swap(*i, *j);
            keel::reverse(ii, last);
            return true;
        }
        if (i == first) {
            keel::reverse(first, last);
            return false;
        }
    }
}

template <typename T, typename LessFn = DefaultLess>
constexpr bool prev_permutation(T* first, T* last, LessFn less = LessFn{}) {
    if (last - first <= 1) return false;
    T* i = last - 1;
    while (true) {
        T* ii = i;
        --i;
        if (less(*ii, *i)) {
            T* j = last;
            while (!less(*(--j), *i)) {
            }
            keel::swap(*i, *j);
            keel::reverse(ii, last);
            return true;
        }
        if (i == first) {
            keel::reverse(first, last);
            return false;
        }
    }
}

// ============================================================================
// Numeric Primitives (replaces <numeric>: accumulate, gcd, lcm)
// ============================================================================

template <typename T, typename Acc>
[[nodiscard]] constexpr Acc accumulate(const T* first, const T* last, Acc init) {
    for (const T* it = first; it < last; ++it) {
        init = keel::move(init) + *it;
    }
    return init;
}

template <typename T, typename Acc, typename BinaryOp>
[[nodiscard]] constexpr Acc accumulate(const T* first, const T* last, Acc init, BinaryOp&& op) {
    for (const T* it = first; it < last; ++it) {
        init = op(keel::move(init), *it);
    }
    return init;
}

namespace detail {

template <typename T> [[nodiscard]] constexpr u64 abs_to_u64(T v) noexcept {
    if constexpr (IsSigned<T>) {
        return v < 0 ? (u64{0} - static_cast<u64>(v)) : static_cast<u64>(v);
    } else {
        return static_cast<u64>(v);
    }
}

[[nodiscard]] constexpr u64 gcd_u64(u64 a, u64 b) noexcept {
    while (b != 0) {
        const u64 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

} // namespace detail

template <typename A, typename B>
    requires(IsIntegral<A> && IsIntegral<B> && !IsSame<RemoveCv<A>, bool> && !IsSame<RemoveCv<B>, bool>)
[[nodiscard]] constexpr RemoveCv<decltype(A{} + B{})> gcd(A a, B b) noexcept {
    using Common = RemoveCv<decltype(A{} + B{})>;
    return static_cast<Common>(detail::gcd_u64(detail::abs_to_u64(a), detail::abs_to_u64(b)));
}

template <typename A, typename B>
    requires(IsIntegral<A> && IsIntegral<B> && !IsSame<RemoveCv<A>, bool> && !IsSame<RemoveCv<B>, bool>)
[[nodiscard]] constexpr RemoveCv<decltype(A{} + B{})> lcm(A a, B b) noexcept {
    using Common = RemoveCv<decltype(A{} + B{})>;
    const u64 ua = detail::abs_to_u64(a);
    const u64 ub = detail::abs_to_u64(b);
    if (ua == 0 || ub == 0) return Common{0};
    return static_cast<Common>((ua / detail::gcd_u64(ua, ub)) * ub);
}

} // namespace keel
