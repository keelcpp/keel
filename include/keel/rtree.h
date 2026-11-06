#pragma once

#include "keel/algo.h"
#include "keel/arena.h"
#include "keel/array.h"
#include "keel/bits.h"
#include "keel/core.h"
#include "keel/ref.h"

namespace keel {

namespace detail {

[[nodiscard]] constexpr u64 upow_saturating(u64 base, isize exp) noexcept {
    u64 result = 1;
    for (isize i = 0; i < exp; ++i) {
        if (base != 0 && result > (~u64{0}) / base) {
            return ~u64{0};
        }
        result *= base;
    }
    return result;
}

// Computes ceil(n^(1 / root)) using exact integer binary search (zero <cmath>).
[[nodiscard]] constexpr isize iroot_ceil(isize n, isize root) noexcept {
    if (n <= 1) return 1;
    if (root <= 1) return n;
    const u64 target = static_cast<u64>(n);
    const int bits = 64 - countl_zero(target);
    u64 lo = 1;
    u64 hi = u64{1} << ((bits + static_cast<int>(root) - 1) / static_cast<int>(root));
    while (lo < hi) {
        const u64 mid = lo + ((hi - lo) >> 1);
        if (upow_saturating(mid, root) < target) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return static_cast<isize>(lo > 0 ? lo : 1);
}

} // namespace detail

template <typename CoordT = f32, isize Dim = 2> struct RTreeBox {
    static_assert(Dim >= 1, "RTreeBox dimension Dim must be >= 1");

    CoordT min[static_cast<usize>(Dim)]{};
    CoordT max[static_cast<usize>(Dim)]{};

    constexpr RTreeBox() noexcept = default;

    constexpr RTreeBox(CoordT min_x, CoordT min_y, CoordT max_x, CoordT max_y) noexcept
        requires(Dim == 2)
        : min{min_x, min_y}, max{max_x, max_y} {}

    constexpr RTreeBox(CoordT min_x, CoordT min_y, CoordT min_z, CoordT max_x, CoordT max_y, CoordT max_z) noexcept
        requires(Dim == 3)
        : min{min_x, min_y, min_z}, max{max_x, max_y, max_z} {}

    [[nodiscard]] static constexpr RTreeBox from_xywh(CoordT x, CoordT y, CoordT w, CoordT h) noexcept
        requires(Dim == 2)
    {
        return RTreeBox(x, y, x + w, y + h);
    }

    [[nodiscard]] static constexpr RTreeBox from_xyzwhd(
        CoordT x, CoordT y, CoordT z, CoordT w, CoordT h, CoordT d
    ) noexcept
        requires(Dim == 3)
    {
        return RTreeBox(x, y, z, x + w, y + h, z + d);
    }

    [[nodiscard]] constexpr CoordT extent(isize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis >= 0 && axis < Dim);
        return max[axis] - min[axis];
    }

    [[nodiscard]] constexpr CoordT center(isize axis) const noexcept {
        KEEL_BOUNDS_ASSERT(axis >= 0 && axis < Dim);
        return (min[axis] + max[axis]) * CoordT(0.5);
    }

    [[nodiscard]] constexpr RTreeBox inflated_min_size(CoordT min_extent) const noexcept {
        RTreeBox out = *this;
        const CoordT half = min_extent * CoordT(0.5);
        for (isize d = 0; d < Dim; ++d) {
            if (out.max[d] - out.min[d] < min_extent) {
                out.min[d] -= half;
                out.max[d] = out.min[d] + min_extent;
            }
        }
        return out;
    }

    [[nodiscard]] constexpr bool contains_point(const CoordT (&pt)[static_cast<usize>(Dim)]) const noexcept {
        for (isize d = 0; d < Dim; ++d) {
            if (pt[d] < min[d] || pt[d] > max[d]) return false;
        }
        return true;
    }

    [[nodiscard]] constexpr bool contains_point(CoordT x, CoordT y) const noexcept
        requires(Dim == 2)
    {
        return x >= min[0] && x <= max[0] && y >= min[1] && y <= max[1];
    }

    [[nodiscard]] constexpr bool contains_point(CoordT x, CoordT y, CoordT z) const noexcept
        requires(Dim == 3)
    {
        return x >= min[0] && x <= max[0] && y >= min[1] && y <= max[1] && z >= min[2] && z <= max[2];
    }

    [[nodiscard]] constexpr bool intersects(const RTreeBox& other) const noexcept {
        for (isize d = 0; d < Dim; ++d) {
            if (min[d] >= other.max[d] || max[d] <= other.min[d]) return false;
        }
        return true;
    }

    [[nodiscard]] constexpr RTreeBox merged(const RTreeBox& other) const noexcept {
        RTreeBox out;
        for (isize d = 0; d < Dim; ++d) {
            out.min[d] = keel::min(min[d], other.min[d]);
            out.max[d] = keel::max(max[d], other.max[d]);
        }
        return out;
    }

    [[nodiscard]] constexpr bool operator==(const RTreeBox& other) const noexcept {
        for (isize d = 0; d < Dim; ++d) {
            if (min[d] != other.min[d] || max[d] != other.max[d]) return false;
        }
        return true;
    }
};

template <typename IdT = u32, typename CoordT = f32, isize Dim = 2> struct RTreeItem {
    IdT id{};
    RTreeBox<CoordT, Dim> box{};
};

// Flat Sort-Tile-Recursive (STR) packed N-dimensional R-Tree for O(log N) bounding-box and point queries.
template <typename IdT = u32, typename CoordT = f32, isize Dim = 2, isize NodeCap = 16> class RTree {
    static_assert(Dim >= 1, "RTree dimension Dim must be >= 1");
    static_assert(NodeCap >= 2 && NodeCap <= 65535, "RTree NodeCap must be in [2..65535]");

public:
    using Box = RTreeBox<CoordT, Dim>;
    using Item = RTreeItem<IdT, CoordT, Dim>;

    struct Node {
        Box bounds{};
        u32 first_child = 0;
        u16 count = 0;
        bool is_leaf = true;
    };

    struct Leaf {
        Box bounds{};
        IdT id{};
    };

    explicit RTree(Allocator* alloc = nullptr) noexcept : m_nodes(alloc), m_leaves(alloc), m_root_index(0) {}

    RTree(const RTree& other)
        : m_nodes(other.m_nodes.clone()),
          m_leaves(other.m_leaves.clone()),
          m_root_index(other.m_root_index) {}

    RTree& operator=(const RTree& other) {
        if (this != &other) {
            m_nodes = other.m_nodes.clone(m_nodes.allocator());
            m_leaves = other.m_leaves.clone(m_leaves.allocator());
            m_root_index = other.m_root_index;
        }
        return *this;
    }

    RTree(RTree&& other) noexcept
        : m_nodes(keel::move(other.m_nodes)),
          m_leaves(keel::move(other.m_leaves)),
          m_root_index(other.m_root_index) {
        other.m_root_index = 0;
    }

    RTree& operator=(RTree&& other) noexcept {
        if (this != &other) {
            m_nodes = keel::move(other.m_nodes);
            m_leaves = keel::move(other.m_leaves);
            m_root_index = other.m_root_index;
            other.m_root_index = 0;
        }
        return *this;
    }

    [[nodiscard]] RTree clone(Allocator* alloc = nullptr) const {
        RTree copy(alloc);
        copy.m_nodes = m_nodes.clone(alloc);
        copy.m_leaves = m_leaves.clone(alloc);
        copy.m_root_index = m_root_index;
        return copy;
    }

    void clear() noexcept {
        m_nodes.clear();
        m_leaves.clear();
        m_root_index = 0;
    }

    [[nodiscard]] bool empty() const noexcept { return m_nodes.empty(); }
    [[nodiscard]] isize node_count() const noexcept { return m_nodes.size(); }
    [[nodiscard]] isize item_count() const noexcept { return m_leaves.size(); }
    [[nodiscard]] u32 root_index() const noexcept { return m_root_index; }
    [[nodiscard]] ArrayRef<Node> nodes() const noexcept { return m_nodes; }
    [[nodiscard]] ArrayRef<Leaf> leaves() const noexcept { return m_leaves; }

    void build(ArrayRef<Item> input_items) {
        clear();
        const isize n = input_items.size();
        if (n <= 0) return;

        ArenaTemp scratch = get_scratch();
        SortEntry* entries = scratch.arena().template alloc_array<SortEntry>(n);
        for (isize i = 0; i < n; ++i) {
            const Box& b = input_items[i].box;
            entries[i].id = input_items[i].id;
            entries[i].box = b;
            for (isize d = 0; d < Dim; ++d) {
                entries[i].center[d] = b.center(d);
            }
        }

        // 1. Recursive N-dimensional Sort-Tile-Recursive (STR) strip partitioning across axes 0..Dim-1
        str_partition_axis(entries, n, 0);

        m_leaves.resize(n);
        for (isize i = 0; i < n; ++i) {
            m_leaves[i] = Leaf{
                .bounds = entries[i].box,
                .id = entries[i].id
            };
        }

        const isize num_leaves = (n + NodeCap - 1) / NodeCap;
        m_nodes.reserve(num_leaves * 2 + 4);

        // 2. Create leaf nodes
        u32 level_start = 0;
        for (isize i = 0; i < n; i += NodeCap) {
            const isize end = keel::min(n, i + NodeCap);
            Box box = entries[i].box;
            for (isize j = i + 1; j < end; ++j) {
                box = box.merged(entries[j].box);
            }
            m_nodes.push_back(Node{
                .bounds = box,
                .first_child = static_cast<u32>(i),
                .count = static_cast<u16>(end - i),
                .is_leaf = true
            });
        }
        u32 level_end = static_cast<u32>(m_nodes.size());

        // 3. Build internal levels bottom-up until a single root node remains
        while (level_end - level_start > 1) {
            const u32 next_level_start = level_end;
            for (u32 i = level_start; i < level_end; i += static_cast<u32>(NodeCap)) {
                const u32 end = keel::min(level_end, i + static_cast<u32>(NodeCap));
                Box box = m_nodes[static_cast<isize>(i)].bounds;
                for (u32 j = i + 1; j < end; ++j) {
                    box = box.merged(m_nodes[static_cast<isize>(j)].bounds);
                }
                m_nodes.push_back(Node{
                    .bounds = box,
                    .first_child = i,
                    .count = static_cast<u16>(end - i),
                    .is_leaf = false
                });
            }
            level_start = next_level_start;
            level_end = static_cast<u32>(m_nodes.size());
        }

        m_root_index = m_nodes.empty() ? 0 : static_cast<u32>(m_nodes.size() - 1);
    }

    template <typename Fn> void query_box(const Box& query, Fn&& on_hit) const {
        if (m_nodes.empty()) return;

        u32 stack[128]{};
        isize sp = 0;
        stack[sp++] = m_root_index;

        while (sp > 0) {
            const Node& node = m_nodes[static_cast<isize>(stack[--sp])];
            if (!node.bounds.intersects(query)) {
                continue;
            }
            const u32 end = node.first_child + node.count;
            if (node.is_leaf) {
                for (u32 i = node.first_child; i < end; ++i) {
                    const Leaf& leaf = m_leaves[static_cast<isize>(i)];
                    if (leaf.bounds.intersects(query)) {
                        on_hit(leaf.id);
                    }
                }
            } else {
                for (u32 i = node.first_child; i < end; ++i) {
                    if (sp < 128) {
                        stack[sp++] = i;
                    }
                }
            }
        }
    }

    void query_box(const Box& query, Array<IdT>& out_ids) const {
        out_ids.clear();
        query_box(query, [&](const IdT& id) { out_ids.push_back(id); });
    }

    template <typename Fn>
    void query_point(const CoordT (&pt)[static_cast<usize>(Dim)], Fn&& on_hit) const {
        if (m_nodes.empty()) return;

        u32 stack[128]{};
        isize sp = 0;
        stack[sp++] = m_root_index;

        while (sp > 0) {
            const Node& node = m_nodes[static_cast<isize>(stack[--sp])];
            if (!node.bounds.contains_point(pt)) {
                continue;
            }
            const u32 end = node.first_child + node.count;
            if (node.is_leaf) {
                for (u32 i = node.first_child; i < end; ++i) {
                    const Leaf& leaf = m_leaves[static_cast<isize>(i)];
                    if (leaf.bounds.contains_point(pt)) {
                        on_hit(leaf.id);
                    }
                }
            } else {
                for (u32 i = node.first_child; i < end; ++i) {
                    if (sp < 128) {
                        stack[sp++] = i;
                    }
                }
            }
        }
    }

    template <typename Fn>
    void query_point(CoordT x, CoordT y, Fn&& on_hit) const
        requires(Dim == 2)
    {
        const CoordT pt[2] = {x, y};
        query_point(pt, keel::forward<Fn>(on_hit));
    }

    template <typename Fn>
    void query_point(CoordT x, CoordT y, CoordT z, Fn&& on_hit) const
        requires(Dim == 3)
    {
        const CoordT pt[3] = {x, y, z};
        query_point(pt, keel::forward<Fn>(on_hit));
    }

private:
    struct SortEntry {
        IdT id{};
        Box box{};
        CoordT center[static_cast<usize>(Dim)]{};
    };

    static void str_partition_axis(SortEntry* entries, isize count, isize axis) noexcept {
        if (count <= 1) return;

        keel::sort(entries, count, [axis](const SortEntry& a, const SortEntry& b) noexcept {
            return a.center[axis] < b.center[axis];
        });

        if (axis + 1 >= Dim || count <= NodeCap) {
            return;
        }

        const isize num_leaves = (count + NodeCap - 1) / NodeCap;
        const isize rem_dims = Dim - axis;
        const isize num_strips = detail::iroot_ceil(num_leaves, rem_dims);
        const isize items_per_strip = (count + num_strips - 1) / num_strips;

        for (isize s = 0; s < count; s += items_per_strip) {
            const isize end = keel::min(count, s + items_per_strip);
            str_partition_axis(entries + s, end - s, axis + 1);
        }
    }

    Array<Node> m_nodes;
    Array<Leaf> m_leaves;
    u32 m_root_index = 0;
};

} // namespace keel
