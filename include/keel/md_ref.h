#pragma once

#include "keel/core.h"
#include "keel/ref.h"
#include "keel/static_array.h"

namespace keel {

namespace detail {
[[nodiscard]] constexpr bool checked_mul_usize(usize a, usize b, usize& out) noexcept {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > kInvalidIndex / b) {
        out = 0;
        return false;
    }
    out = a * b;
    return true;
}

template <typename I> [[nodiscard]] constexpr usize normalize_extent(I value) noexcept {
    if constexpr (IsSigned<RemoveCvRef<I>>) {
        return (value > 0) ? static_cast<usize>(value) : 0;
    } else {
        return static_cast<usize>(value);
    }
}

template <usize Rank>
[[nodiscard]] constexpr bool compute_md_extents(
    const StaticArray<usize, Rank>& shape,
    usize& outTotal
) noexcept {
    usize total = 1;
    for (usize d = 0; d < Rank; ++d) {
        if (shape[d] == 0) {
            outTotal = 0;
            return false;
        }
        if (!checked_mul_usize(total, shape[d], total)) {
            outTotal = 0;
            return false;
        }
    }
    outTotal = total;
    return true;
}

template <usize Rank>
[[nodiscard]] constexpr StaticArray<usize, Rank> compute_contiguous_strides(
    const StaticArray<usize, Rank>& shape
) noexcept {
    StaticArray<usize, Rank> strides{};
    usize s = 1;
    for (usize idx = Rank; idx > 0; --idx) {
        const usize d = idx - 1;
        strides[d] = s;
        s *= shape[d];
    }
    return strides;
}

template <usize Rank>
[[nodiscard]] constexpr bool validate_strided_layout(
    const StaticArray<usize, Rank>& shape,
    const StaticArray<usize, Rank>& strides,
    usize bufSize
) noexcept {
    usize logicalTotal = 0;
    if (!compute_md_extents(shape, logicalTotal)) {
        return false;
    }
    usize maxOffset = 0;
    for (usize d = 0; d < Rank; ++d) {
        usize dimSpan = 0;
        if (!checked_mul_usize(shape[d] - 1, strides[d], dimSpan)) {
            return false;
        }
        if (maxOffset > kInvalidIndex - dimSpan) {
            return false;
        }
        maxOffset += dimSpan;
    }
    return maxOffset < bufSize;
}

template <usize Rank>
[[nodiscard]] constexpr usize compute_md_offset(
    const StaticArray<usize, Rank>& extents,
    const StaticArray<usize, Rank>& coords
) noexcept {
    usize offset = 0;
    for (usize d = 0; d < Rank; ++d) {
        KEEL_BOUNDS_ASSERT(coords[d] < extents[d]);
        offset = offset * extents[d] + coords[d];
    }
    return offset;
}

template <usize Rank>
[[nodiscard]] constexpr usize compute_strided_offset(
    const StaticArray<usize, Rank>& extents,
    const StaticArray<usize, Rank>& strides,
    const StaticArray<usize, Rank>& coords
) noexcept {
    usize offset = 0;
    for (usize d = 0; d < Rank; ++d) {
        KEEL_BOUNDS_ASSERT(coords[d] < extents[d]);
        offset += coords[d] * strides[d];
    }
    return offset;
}

template <usize Rank>
[[nodiscard]] constexpr bool is_valid_permutation(const StaticArray<usize, Rank>& order) noexcept {
    StaticArray<bool, Rank> seen{};
    for (usize i = 0; i < Rank; ++i) {
        if (order[i] >= Rank || seen[order[i]]) {
            return false;
        }
        seen[order[i]] = true;
    }
    return true;
}

template <typename T, usize Axis, usize Rank>
constexpr void fill_strided_impl(
    T* ptr,
    const StaticArray<usize, Rank>& extents,
    const StaticArray<usize, Rank>& strides,
    const T& value
) noexcept {
    const usize n = extents[Axis];
    const usize st = strides[Axis];
    if constexpr (Axis + 1 == Rank) {
        if (st == 1) {
            for (usize i = 0; i < n; ++i) {
                ptr[i] = value;
            }
        } else {
            for (usize i = 0; i < n; ++i) {
                ptr[i * st] = value;
            }
        }
    } else {
        for (usize i = 0; i < n; ++i) {
            fill_strided_impl<T, Axis + 1, Rank>(ptr + i * st, extents, strides, value);
        }
    }
}
} // namespace detail

template <typename T, usize Rank> class MutMdRef;
template <typename T, usize Rank> class StridedMdRef;
template <typename T, usize Rank> class MutStridedMdRef;

// Non-owning contiguous row-major N-dimensional view (overflow-safe shape validation on construction).
template <typename T, usize Rank> class MdRef {
    static_assert(Rank >= 1, "MdRef requires Rank >= 1");

public:
    constexpr MdRef() noexcept = default;
    constexpr MdRef(decltype(nullptr)) noexcept {}

    constexpr MdRef(ArrayRef<T> buf) noexcept
        requires(Rank == 1)
    {
        if (buf.data() != nullptr && buf.size() > 0) {
            m_data = buf.data();
            m_extents[0] = buf.size();
        }
    }

    constexpr MdRef(ArrayRef<T> buf, const StaticArray<usize, Rank>& shape) noexcept {
        usize total = 0;
        if (buf.data() != nullptr && detail::compute_md_extents(shape, total) && buf.size() >= total) {
            m_data = buf.data();
            m_extents = shape;
        }
    }

    template <typename... Extents>
        requires(sizeof...(Extents) == Rank) && (IsIntegral<RemoveCvRef<Extents>> && ...)
    constexpr MdRef(ArrayRef<T> buf, Extents... dims) noexcept
        : MdRef(buf, StaticArray<usize, Rank>{{detail::normalize_extent(dims)...}}) {}

    [[nodiscard]] static constexpr usize rank() noexcept { return Rank; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_data == nullptr || m_extents[0] == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return !empty(); }

    [[nodiscard]] constexpr const T* data() const noexcept { return m_data; }
    [[nodiscard]] constexpr const StaticArray<usize, Rank>& shape() const noexcept { return m_extents; }

    [[nodiscard]] constexpr usize extent(usize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis < Rank);
        return m_extents[axis];
    }

    [[nodiscard]] constexpr usize size() const noexcept {
        if (empty()) return 0;
        usize total = 1;
        for (usize d = 0; d < Rank; ++d) {
            total *= m_extents[d];
        }
        return total;
    }

    [[nodiscard]] constexpr usize stride(usize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis < Rank);
        if (empty()) return 0;
        usize s = 1;
        for (usize d = axis + 1; d < Rank; ++d) {
            s *= m_extents[d];
        }
        return s;
    }

    [[nodiscard]] constexpr ArrayRef<T> flat() const noexcept {
        return ArrayRef<T>(m_data, size());
    }

    [[nodiscard]] constexpr StridedMdRef<T, Rank> strided() const noexcept;

    template <typename... Indices>
        requires(sizeof...(Indices) == Rank) && (IsIntegral<RemoveCvRef<Indices>> && ...)
    [[nodiscard]] constexpr const T& at(Indices... idx) const noexcept {
        KEEL_BOUNDS_ASSERT(!empty());
        const StaticArray<usize, Rank> coords{{detail::normalize_extent(idx)...}};
        return m_data[detail::compute_md_offset(m_extents, coords)];
    }

    template <typename... Indices>
        requires(sizeof...(Indices) == Rank) && (IsIntegral<RemoveCvRef<Indices>> && ...)
    [[nodiscard]] constexpr const T& operator()(Indices... idx) const noexcept {
        return at(idx...);
    }

    [[nodiscard]] constexpr MdRef<T, Rank - 1> subview(usize i) const noexcept
        requires(Rank >= 2)
    {
        KEEL_BOUNDS_ASSERT(!empty() && i < m_extents[0]);
        const usize outerStride = stride(0);
        StaticArray<usize, Rank - 1> subShape{};
        for (usize d = 1; d < Rank; ++d) {
            subShape[d - 1] = m_extents[d];
        }
        return MdRef<T, Rank - 1>(ArrayRef<T>(m_data + i * outerStride, outerStride), subShape);
    }

    [[nodiscard]] constexpr ArrayRef<T> row(usize i) const noexcept
        requires(Rank == 2)
    {
        KEEL_BOUNDS_ASSERT(!empty() && i < m_extents[0]);
        return ArrayRef<T>(m_data + i * m_extents[1], m_extents[1]);
    }

    [[nodiscard]] constexpr MdRef<T, Rank> slice(usize start, usize count = kInvalidIndex) const noexcept {
        if (empty()) return {};
        KEEL_BOUNDS_ASSERT(start <= m_extents[0]);
        const usize rem = m_extents[0] - start;
        const usize len = (count > rem) ? rem : count;
        if (len == 0) return {};
        const usize outerStride = stride(0);
        StaticArray<usize, Rank> newShape = m_extents;
        newShape[0] = len;
        return MdRef<T, Rank>(ArrayRef<T>(m_data + start * outerStride, len * outerStride), newShape);
    }

    template <usize NewRank>
    [[nodiscard]] constexpr MdRef<T, NewRank> reshape(const StaticArray<usize, NewRank>& newShape) const noexcept {
        if (empty()) return {};
        usize newTotal = 0;
        if (!detail::compute_md_extents(newShape, newTotal) || newTotal != size()) {
            return {};
        }
        return MdRef<T, NewRank>(flat(), newShape);
    }

    template <typename... Extents>
        requires(sizeof...(Extents) >= 1) && (IsIntegral<RemoveCvRef<Extents>> && ...)
    [[nodiscard]] constexpr MdRef<T, sizeof...(Extents)> reshape(Extents... dims) const noexcept {
        return reshape(StaticArray<usize, sizeof...(Extents)>{{detail::normalize_extent(dims)...}});
    }

    [[nodiscard]] friend constexpr bool operator==(MdRef a, MdRef b) noexcept
        requires requires(const T& x) {
            { x == x } -> detail::ConvertibleTo<bool>;
        }
    {
        if (a.empty() && b.empty()) return true;
        return a.m_extents == b.m_extents && a.flat() == b.flat();
    }

    [[nodiscard]] friend constexpr bool operator!=(MdRef a, MdRef b) noexcept
        requires requires(const T& x) {
            { x == x } -> detail::ConvertibleTo<bool>;
        }
    {
        return !(a == b);
    }

private:
    const T* m_data = nullptr;
    StaticArray<usize, Rank> m_extents{};
};

// Mutable non-owning contiguous row-major N-dimensional view.
template <typename T, usize Rank> class MutMdRef {
    static_assert(Rank >= 1, "MutMdRef requires Rank >= 1");

public:
    constexpr MutMdRef() noexcept = default;
    constexpr MutMdRef(decltype(nullptr)) noexcept {}

    constexpr MutMdRef(MutArrayRef<T> buf) noexcept
        requires(Rank == 1)
    {
        if (buf.data() != nullptr && buf.size() > 0) {
            m_data = buf.data();
            m_extents[0] = buf.size();
        }
    }

    constexpr MutMdRef(MutArrayRef<T> buf, const StaticArray<usize, Rank>& shape) noexcept {
        usize total = 0;
        if (buf.data() != nullptr && detail::compute_md_extents(shape, total) && buf.size() >= total) {
            m_data = buf.data();
            m_extents = shape;
        }
    }

    template <typename... Extents>
        requires(sizeof...(Extents) == Rank) && (IsIntegral<RemoveCvRef<Extents>> && ...)
    constexpr MutMdRef(MutArrayRef<T> buf, Extents... dims) noexcept
        : MutMdRef(buf, StaticArray<usize, Rank>{{detail::normalize_extent(dims)...}}) {}

    [[nodiscard]] constexpr operator MdRef<T, Rank>() const noexcept {
        return MdRef<T, Rank>(ArrayRef<T>(m_data, size()), m_extents);
    }

    [[nodiscard]] constexpr MdRef<T, Rank> as_ref() const noexcept {
        return static_cast<MdRef<T, Rank>>(*this);
    }

    [[nodiscard]] constexpr MutStridedMdRef<T, Rank> strided() const noexcept;

    [[nodiscard]] static constexpr usize rank() noexcept { return Rank; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_data == nullptr || m_extents[0] == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return !empty(); }

    [[nodiscard]] constexpr T* data() const noexcept { return m_data; }
    [[nodiscard]] constexpr const StaticArray<usize, Rank>& shape() const noexcept { return m_extents; }

    [[nodiscard]] constexpr usize extent(usize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis < Rank);
        return m_extents[axis];
    }

    [[nodiscard]] constexpr usize size() const noexcept {
        if (empty()) return 0;
        usize total = 1;
        for (usize d = 0; d < Rank; ++d) {
            total *= m_extents[d];
        }
        return total;
    }

    [[nodiscard]] constexpr usize stride(usize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis < Rank);
        if (empty()) return 0;
        usize s = 1;
        for (usize d = axis + 1; d < Rank; ++d) {
            s *= m_extents[d];
        }
        return s;
    }

    [[nodiscard]] constexpr MutArrayRef<T> flat() const noexcept {
        return MutArrayRef<T>(m_data, size());
    }

    template <typename... Indices>
        requires(sizeof...(Indices) == Rank) && (IsIntegral<RemoveCvRef<Indices>> && ...)
    [[nodiscard]] constexpr T& at(Indices... idx) const noexcept {
        KEEL_BOUNDS_ASSERT(!empty());
        const StaticArray<usize, Rank> coords{{detail::normalize_extent(idx)...}};
        return m_data[detail::compute_md_offset(m_extents, coords)];
    }

    template <typename... Indices>
        requires(sizeof...(Indices) == Rank) && (IsIntegral<RemoveCvRef<Indices>> && ...)
    [[nodiscard]] constexpr T& operator()(Indices... idx) const noexcept {
        return at(idx...);
    }

    [[nodiscard]] constexpr MutMdRef<T, Rank - 1> subview(usize i) const noexcept
        requires(Rank >= 2)
    {
        KEEL_BOUNDS_ASSERT(!empty() && i < m_extents[0]);
        const usize outerStride = stride(0);
        StaticArray<usize, Rank - 1> subShape{};
        for (usize d = 1; d < Rank; ++d) {
            subShape[d - 1] = m_extents[d];
        }
        return MutMdRef<T, Rank - 1>(MutArrayRef<T>(m_data + i * outerStride, outerStride), subShape);
    }

    [[nodiscard]] constexpr MutArrayRef<T> row(usize i) const noexcept
        requires(Rank == 2)
    {
        KEEL_BOUNDS_ASSERT(!empty() && i < m_extents[0]);
        return MutArrayRef<T>(m_data + i * m_extents[1], m_extents[1]);
    }

    [[nodiscard]] constexpr MutMdRef<T, Rank> slice(usize start, usize count = kInvalidIndex) const noexcept {
        if (empty()) return {};
        KEEL_BOUNDS_ASSERT(start <= m_extents[0]);
        const usize rem = m_extents[0] - start;
        const usize len = (count > rem) ? rem : count;
        if (len == 0) return {};
        const usize outerStride = stride(0);
        StaticArray<usize, Rank> newShape = m_extents;
        newShape[0] = len;
        return MutMdRef<T, Rank>(MutArrayRef<T>(m_data + start * outerStride, len * outerStride), newShape);
    }

    template <usize NewRank>
    [[nodiscard]] constexpr MutMdRef<T, NewRank> reshape(const StaticArray<usize, NewRank>& newShape) const noexcept {
        if (empty()) return {};
        usize newTotal = 0;
        if (!detail::compute_md_extents(newShape, newTotal) || newTotal != size()) {
            return {};
        }
        return MutMdRef<T, NewRank>(flat(), newShape);
    }

    template <typename... Extents>
        requires(sizeof...(Extents) >= 1) && (IsIntegral<RemoveCvRef<Extents>> && ...)
    [[nodiscard]] constexpr MutMdRef<T, sizeof...(Extents)> reshape(Extents... dims) const noexcept {
        return reshape(StaticArray<usize, sizeof...(Extents)>{{detail::normalize_extent(dims)...}});
    }

    constexpr void fill(const T& value) const noexcept {
        const usize total = size();
        for (usize i = 0; i < total; ++i) {
            m_data[i] = value;
        }
    }

private:
    T* m_data = nullptr;
    StaticArray<usize, Rank> m_extents{};
};

// Non-owning N-dimensional view supporting arbitrary element strides (ROI crops, column views, decimation, transposes).
template <typename T, usize Rank> class StridedMdRef {
    static_assert(Rank >= 1, "StridedMdRef requires Rank >= 1");

public:
    constexpr StridedMdRef() noexcept = default;
    constexpr StridedMdRef(decltype(nullptr)) noexcept {}

    constexpr StridedMdRef(MdRef<T, Rank> contiguous) noexcept {
        if (!contiguous.empty()) {
            m_data = contiguous.data();
            m_extents = contiguous.shape();
            m_strides = detail::compute_contiguous_strides(m_extents);
        }
    }

    constexpr StridedMdRef(
        ArrayRef<T> buf,
        const StaticArray<usize, Rank>& shape,
        const StaticArray<usize, Rank>& strides
    ) noexcept {
        if (buf.data() != nullptr && detail::validate_strided_layout(shape, strides, buf.size())) {
            m_data = buf.data();
            m_extents = shape;
            m_strides = strides;
        }
    }

    [[nodiscard]] static constexpr usize rank() noexcept { return Rank; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_data == nullptr || m_extents[0] == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return !empty(); }

    [[nodiscard]] constexpr const T* data() const noexcept { return m_data; }
    [[nodiscard]] constexpr const StaticArray<usize, Rank>& shape() const noexcept { return m_extents; }
    [[nodiscard]] constexpr const StaticArray<usize, Rank>& strides() const noexcept { return m_strides; }

    [[nodiscard]] constexpr usize extent(usize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis < Rank);
        return m_extents[axis];
    }

    [[nodiscard]] constexpr usize stride(usize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis < Rank);
        return m_strides[axis];
    }

    [[nodiscard]] constexpr usize size() const noexcept {
        if (empty()) return 0;
        usize total = 1;
        for (usize d = 0; d < Rank; ++d) {
            total *= m_extents[d];
        }
        return total;
    }

    [[nodiscard]] constexpr bool is_contiguous() const noexcept {
        if (empty()) return false;
        usize expected = 1;
        for (usize idx = Rank; idx > 0; --idx) {
            const usize d = idx - 1;
            if (m_strides[d] != expected) {
                return false;
            }
            expected *= m_extents[d];
        }
        return true;
    }

    [[nodiscard]] constexpr MdRef<T, Rank> as_contiguous() const noexcept {
        if (!is_contiguous()) return {};
        return MdRef<T, Rank>(ArrayRef<T>(m_data, size()), m_extents);
    }

    template <typename... Indices>
        requires(sizeof...(Indices) == Rank) && (IsIntegral<RemoveCvRef<Indices>> && ...)
    [[nodiscard]] constexpr const T& at(Indices... idx) const noexcept {
        KEEL_BOUNDS_ASSERT(!empty());
        const StaticArray<usize, Rank> coords{{detail::normalize_extent(idx)...}};
        return m_data[detail::compute_strided_offset(m_extents, m_strides, coords)];
    }

    template <typename... Indices>
        requires(sizeof...(Indices) == Rank) && (IsIntegral<RemoveCvRef<Indices>> && ...)
    [[nodiscard]] constexpr const T& operator()(Indices... idx) const noexcept {
        return at(idx...);
    }

    [[nodiscard]] constexpr StridedMdRef<T, Rank - 1> subview(usize axis, usize index) const noexcept
        requires(Rank >= 2)
    {
        KEEL_BOUNDS_ASSERT(!empty() && axis < Rank && index < m_extents[axis]);
        StaticArray<usize, Rank - 1> subShape{};
        StaticArray<usize, Rank - 1> subStrides{};
        usize dst = 0;
        for (usize d = 0; d < Rank; ++d) {
            if (d == axis) continue;
            subShape[dst] = m_extents[d];
            subStrides[dst] = m_strides[d];
            ++dst;
        }
        return StridedMdRef<T, Rank - 1>::from_trusted(m_data + index * m_strides[axis], subShape, subStrides);
    }

    [[nodiscard]] constexpr StridedMdRef<T, Rank - 1> subview(usize i) const noexcept
        requires(Rank >= 2)
    {
        return subview(0, i);
    }

    [[nodiscard]] constexpr StridedMdRef<T, 1> row(usize y) const noexcept
        requires(Rank == 2)
    {
        return subview(0, y);
    }

    [[nodiscard]] constexpr StridedMdRef<T, 1> col(usize x) const noexcept
        requires(Rank == 2)
    {
        return subview(1, x);
    }

    [[nodiscard]] constexpr StridedMdRef<T, Rank> slice_axis(
        usize axis,
        usize start,
        usize count = kInvalidIndex,
        usize step = 1
    ) const noexcept {
        if (empty() || axis >= Rank || step == 0 || start >= m_extents[axis] || count == 0) {
            return {};
        }
        const usize available = ((m_extents[axis] - 1 - start) / step) + 1;
        const usize len = (count < available) ? count : available;
        StaticArray<usize, Rank> newShape = m_extents;
        StaticArray<usize, Rank> newStrides = m_strides;
        newShape[axis] = len;
        if (len > 1) {
            newStrides[axis] = m_strides[axis] * step;
        }
        return from_trusted(m_data + start * m_strides[axis], newShape, newStrides);
    }

    [[nodiscard]] constexpr StridedMdRef<T, Rank> permute(const StaticArray<usize, Rank>& order) const noexcept {
        if (empty() || !detail::is_valid_permutation(order)) {
            return {};
        }
        StaticArray<usize, Rank> newShape{};
        StaticArray<usize, Rank> newStrides{};
        for (usize d = 0; d < Rank; ++d) {
            newShape[d] = m_extents[order[d]];
            newStrides[d] = m_strides[order[d]];
        }
        return from_trusted(m_data, newShape, newStrides);
    }

    [[nodiscard]] constexpr StridedMdRef<T, 2> transpose() const noexcept
        requires(Rank == 2)
    {
        return permute(StaticArray<usize, 2>{{1, 0}});
    }

private:
    friend class MutStridedMdRef<T, Rank>;

    template <typename, usize> friend class StridedMdRef;

    [[nodiscard]] static constexpr StridedMdRef from_trusted(
        const T* ptr,
        const StaticArray<usize, Rank>& shape,
        const StaticArray<usize, Rank>& strides
    ) noexcept {
        StridedMdRef v{};
        v.m_data = ptr;
        v.m_extents = shape;
        v.m_strides = strides;
        return v;
    }

    const T* m_data = nullptr;
    StaticArray<usize, Rank> m_extents{};
    StaticArray<usize, Rank> m_strides{};
};

// Mutable non-owning N-dimensional view supporting arbitrary element strides.
template <typename T, usize Rank> class MutStridedMdRef {
    static_assert(Rank >= 1, "MutStridedMdRef requires Rank >= 1");

public:
    constexpr MutStridedMdRef() noexcept = default;
    constexpr MutStridedMdRef(decltype(nullptr)) noexcept {}

    constexpr MutStridedMdRef(MutMdRef<T, Rank> contiguous) noexcept {
        if (!contiguous.empty()) {
            m_data = contiguous.data();
            m_extents = contiguous.shape();
            m_strides = detail::compute_contiguous_strides(m_extents);
        }
    }

    constexpr MutStridedMdRef(
        MutArrayRef<T> buf,
        const StaticArray<usize, Rank>& shape,
        const StaticArray<usize, Rank>& strides
    ) noexcept {
        if (buf.data() != nullptr && detail::validate_strided_layout(shape, strides, buf.size())) {
            m_data = buf.data();
            m_extents = shape;
            m_strides = strides;
        }
    }

    [[nodiscard]] constexpr operator StridedMdRef<T, Rank>() const noexcept {
        return StridedMdRef<T, Rank>::from_trusted(m_data, m_extents, m_strides);
    }

    [[nodiscard]] constexpr StridedMdRef<T, Rank> as_ref() const noexcept {
        return static_cast<StridedMdRef<T, Rank>>(*this);
    }

    [[nodiscard]] static constexpr usize rank() noexcept { return Rank; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_data == nullptr || m_extents[0] == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return !empty(); }

    [[nodiscard]] constexpr T* data() const noexcept { return m_data; }
    [[nodiscard]] constexpr const StaticArray<usize, Rank>& shape() const noexcept { return m_extents; }
    [[nodiscard]] constexpr const StaticArray<usize, Rank>& strides() const noexcept { return m_strides; }

    [[nodiscard]] constexpr usize extent(usize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis < Rank);
        return m_extents[axis];
    }

    [[nodiscard]] constexpr usize stride(usize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis < Rank);
        return m_strides[axis];
    }

    [[nodiscard]] constexpr usize size() const noexcept {
        if (empty()) return 0;
        usize total = 1;
        for (usize d = 0; d < Rank; ++d) {
            total *= m_extents[d];
        }
        return total;
    }

    [[nodiscard]] constexpr bool is_contiguous() const noexcept {
        if (empty()) return false;
        usize expected = 1;
        for (usize idx = Rank; idx > 0; --idx) {
            const usize d = idx - 1;
            if (m_strides[d] != expected) {
                return false;
            }
            expected *= m_extents[d];
        }
        return true;
    }

    [[nodiscard]] constexpr MutMdRef<T, Rank> as_contiguous() const noexcept {
        if (!is_contiguous()) return {};
        return MutMdRef<T, Rank>(MutArrayRef<T>(m_data, size()), m_extents);
    }

    template <typename... Indices>
        requires(sizeof...(Indices) == Rank) && (IsIntegral<RemoveCvRef<Indices>> && ...)
    [[nodiscard]] constexpr T& at(Indices... idx) const noexcept {
        KEEL_BOUNDS_ASSERT(!empty());
        const StaticArray<usize, Rank> coords{{detail::normalize_extent(idx)...}};
        return m_data[detail::compute_strided_offset(m_extents, m_strides, coords)];
    }

    template <typename... Indices>
        requires(sizeof...(Indices) == Rank) && (IsIntegral<RemoveCvRef<Indices>> && ...)
    [[nodiscard]] constexpr T& operator()(Indices... idx) const noexcept {
        return at(idx...);
    }

    [[nodiscard]] constexpr MutStridedMdRef<T, Rank - 1> subview(usize axis, usize index) const noexcept
        requires(Rank >= 2)
    {
        KEEL_BOUNDS_ASSERT(!empty() && axis < Rank && index < m_extents[axis]);
        StaticArray<usize, Rank - 1> subShape{};
        StaticArray<usize, Rank - 1> subStrides{};
        usize dst = 0;
        for (usize d = 0; d < Rank; ++d) {
            if (d == axis) continue;
            subShape[dst] = m_extents[d];
            subStrides[dst] = m_strides[d];
            ++dst;
        }
        return MutStridedMdRef<T, Rank - 1>::from_trusted(m_data + index * m_strides[axis], subShape, subStrides);
    }

    [[nodiscard]] constexpr MutStridedMdRef<T, Rank - 1> subview(usize i) const noexcept
        requires(Rank >= 2)
    {
        return subview(0, i);
    }

    [[nodiscard]] constexpr MutStridedMdRef<T, 1> row(usize y) const noexcept
        requires(Rank == 2)
    {
        return subview(0, y);
    }

    [[nodiscard]] constexpr MutStridedMdRef<T, 1> col(usize x) const noexcept
        requires(Rank == 2)
    {
        return subview(1, x);
    }

    [[nodiscard]] constexpr MutStridedMdRef<T, Rank> slice_axis(
        usize axis,
        usize start,
        usize count = kInvalidIndex,
        usize step = 1
    ) const noexcept {
        if (empty() || axis >= Rank || step == 0 || start >= m_extents[axis] || count == 0) {
            return {};
        }
        const usize available = ((m_extents[axis] - 1 - start) / step) + 1;
        const usize len = (count < available) ? count : available;
        StaticArray<usize, Rank> newShape = m_extents;
        StaticArray<usize, Rank> newStrides = m_strides;
        newShape[axis] = len;
        if (len > 1) {
            newStrides[axis] = m_strides[axis] * step;
        }
        return from_trusted(m_data + start * m_strides[axis], newShape, newStrides);
    }

    [[nodiscard]] constexpr MutStridedMdRef<T, Rank> permute(const StaticArray<usize, Rank>& order) const noexcept {
        if (empty() || !detail::is_valid_permutation(order)) {
            return {};
        }
        StaticArray<usize, Rank> newShape{};
        StaticArray<usize, Rank> newStrides{};
        for (usize d = 0; d < Rank; ++d) {
            newShape[d] = m_extents[order[d]];
            newStrides[d] = m_strides[order[d]];
        }
        return from_trusted(m_data, newShape, newStrides);
    }

    [[nodiscard]] constexpr MutStridedMdRef<T, 2> transpose() const noexcept
        requires(Rank == 2)
    {
        return permute(StaticArray<usize, 2>{{1, 0}});
    }

    constexpr void fill(const T& value) const noexcept {
        if (empty()) return;
        detail::fill_strided_impl<T, 0, Rank>(m_data, m_extents, m_strides, value);
    }

private:
    template <typename, usize> friend class MutStridedMdRef;

    [[nodiscard]] static constexpr MutStridedMdRef from_trusted(
        T* ptr,
        const StaticArray<usize, Rank>& shape,
        const StaticArray<usize, Rank>& strides
    ) noexcept {
        MutStridedMdRef v{};
        v.m_data = ptr;
        v.m_extents = shape;
        v.m_strides = strides;
        return v;
    }

    T* m_data = nullptr;
    StaticArray<usize, Rank> m_extents{};
    StaticArray<usize, Rank> m_strides{};
};

template <typename T, usize Rank>
constexpr StridedMdRef<T, Rank> MdRef<T, Rank>::strided() const noexcept {
    return StridedMdRef<T, Rank>(*this);
}

template <typename T, usize Rank>
constexpr MutStridedMdRef<T, Rank> MutMdRef<T, Rank>::strided() const noexcept {
    return MutStridedMdRef<T, Rank>(*this);
}

} // namespace keel
