#pragma once

#include "keel/algo.h"
#include "keel/arena.h"
#include "keel/array.h"
#include "keel/bits.h"
#include "keel/core.h"
#include "keel/ref.h"

namespace keel {

namespace detail {

[[nodiscard]] constexpr isize isqrt_ceil(isize n) noexcept {
    if (n <= 1) return 1;
    const u64 x = static_cast<u64>(n);
    u64 r = u64{1} << ((64 - countl_zero(x) + 1) / 2);
    while (true) {
        const u64 next = (r + x / r) >> 1;
        if (next >= r) break;
        r = next;
    }
    while (r * r > x) --r;
    if (r * r < x) ++r;
    return static_cast<isize>(r > 0 ? r : 1);
}

} // namespace detail

template <typename CoordT = f32> struct RTreeBox {
    CoordT min_x{};
    CoordT min_y{};
    CoordT max_x{};
    CoordT max_y{};

    constexpr RTreeBox() noexcept = default;
    constexpr RTreeBox(CoordT min_x_, CoordT min_y_, CoordT max_x_, CoordT max_y_) noexcept
        : min_x(min_x_), min_y(min_y_), max_x(max_x_), max_y(max_y_) {}

    [[nodiscard]] static constexpr RTreeBox from_xywh(CoordT x, CoordT y, CoordT w, CoordT h) noexcept {
        return RTreeBox(x, y, x + w, y + h);
    }

    [[nodiscard]] constexpr CoordT width() const noexcept { return max_x - min_x; }
    [[nodiscard]] constexpr CoordT height() const noexcept { return max_y - min_y; }
    [[nodiscard]] constexpr CoordT center_x() const noexcept { return (min_x + max_x) * CoordT(0.5); }
    [[nodiscard]] constexpr CoordT center_y() const noexcept { return (min_y + max_y) * CoordT(0.5); }

    [[nodiscard]] constexpr RTreeBox inflated_min_size(CoordT min_extent) const noexcept {
        RTreeBox out = *this;
        if (out.max_x - out.min_x < min_extent) {
            const CoordT half = min_extent * CoordT(0.5);
            out.min_x -= half;
            out.max_x = out.min_x + min_extent;
        }
        if (out.max_y - out.min_y < min_extent) {
            const CoordT half = min_extent * CoordT(0.5);
            out.min_y -= half;
            out.max_y = out.min_y + min_extent;
        }
        return out;
    }

    [[nodiscard]] constexpr bool contains_point(CoordT x, CoordT y) const noexcept {
        return x >= min_x && x <= max_x && y >= min_y && y <= max_y;
    }

    [[nodiscard]] constexpr bool intersects(const RTreeBox& other) const noexcept {
        return min_x < other.max_x && max_x > other.min_x && min_y < other.max_y && max_y > other.min_y;
    }

    [[nodiscard]] constexpr RTreeBox merged(const RTreeBox& other) const noexcept {
        return RTreeBox(
            keel::min(min_x, other.min_x),
            keel::min(min_y, other.min_y),
            keel::max(max_x, other.max_x),
            keel::max(max_y, other.max_y)
        );
    }

    [[nodiscard]] constexpr bool operator==(const RTreeBox& other) const noexcept {
        return min_x == other.min_x && min_y == other.min_y && max_x == other.max_x && max_y == other.max_y;
    }
};

template <typename IdT = u32, typename CoordT = f32> struct RTreeItem {
    IdT id{};
    RTreeBox<CoordT> box{};
};

// Flat Sort-Tile-Recursive (STR) packed 2D R-Tree for O(log N) bounding-box and point queries.
template <typename IdT = u32, typename CoordT = f32, isize NodeCap = 16> class RTree {
    static_assert(NodeCap >= 2 && NodeCap <= 65535, "RTree NodeCap must be in [2..65535]");

public:
    using Box = RTreeBox<CoordT>;
    using Item = RTreeItem<IdT, CoordT>;

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

        struct SortEntry {
            IdT id;
            Box box;
            CoordT cx;
            CoordT cy;
        };

        ArenaTemp scratch = get_scratch();
        SortEntry* entries = scratch.arena().template alloc_array<SortEntry>(n);
        for (isize i = 0; i < n; ++i) {
            const Box& b = input_items[i].box;
            entries[i] = SortEntry{
                .id = input_items[i].id,
                .box = b,
                .cx = b.center_x(),
                .cy = b.center_y()
            };
        }

        // 1. Sort items by X, slice into vertical strips, and sort each strip by Y (STR packing)
        keel::sort(entries, n, [](const SortEntry& a, const SortEntry& b) noexcept { return a.cx < b.cx; });

        const isize num_leaves = (n + NodeCap - 1) / NodeCap;
        const isize num_strips = detail::isqrt_ceil(num_leaves);
        const isize items_per_strip = (n + num_strips - 1) / num_strips;

        for (isize s = 0; s < n; s += items_per_strip) {
            const isize end = keel::min(n, s + items_per_strip);
            keel::sort(entries + s, end - s, [](const SortEntry& a, const SortEntry& b) noexcept {
                return a.cy < b.cy;
            });
        }

        m_leaves.resize(n);
        for (isize i = 0; i < n; ++i) {
            m_leaves[i] = Leaf{
                .bounds = entries[i].box,
                .id = entries[i].id
            };
        }

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

    template <typename Fn> void query_point(CoordT x, CoordT y, Fn&& on_hit) const {
        if (m_nodes.empty()) return;

        u32 stack[128]{};
        isize sp = 0;
        stack[sp++] = m_root_index;

        while (sp > 0) {
            const Node& node = m_nodes[static_cast<isize>(stack[--sp])];
            if (!node.bounds.contains_point(x, y)) {
                continue;
            }
            const u32 end = node.first_child + node.count;
            if (node.is_leaf) {
                for (u32 i = node.first_child; i < end; ++i) {
                    const Leaf& leaf = m_leaves[static_cast<isize>(i)];
                    if (leaf.bounds.contains_point(x, y)) {
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

private:
    Array<Node> m_nodes;
    Array<Leaf> m_leaves;
    u32 m_root_index = 0;
};

} // namespace keel
