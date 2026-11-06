#include "keel/keel.h"

#if !defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS != 0
#include "keel/interop_std.h"
#endif

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace keel;

namespace {

int g_tests_passed = 0;
int g_tests_failed = 0;

#define TEST_CHECK(cond)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                                     \
            ++g_tests_failed;                                                                                          \
        } else {                                                                                                       \
            ++g_tests_passed;                                                                                          \
        }                                                                                                              \
    } while (0)

struct TrackedObj {
    int value = 0;
    inline static int live_count = 0;

    TrackedObj() noexcept : value(0) { ++live_count; }
    explicit TrackedObj(int v) noexcept : value(v) { ++live_count; }
    TrackedObj(const TrackedObj& o) noexcept : value(o.value) { ++live_count; }
    TrackedObj(TrackedObj&& o) noexcept : value(o.value) {
        o.value = -1;
        ++live_count;
    }
    TrackedObj& operator=(const TrackedObj& o) noexcept {
        value = o.value;
        return *this;
    }
    TrackedObj& operator=(TrackedObj&& o) noexcept {
        value = o.value;
        o.value = -1;
        return *this;
    }
    ~TrackedObj() noexcept { --live_count; }

    bool operator==(const TrackedObj& o) const noexcept { return value == o.value; }
};

void test_core_and_bits() {
    static_assert(sizeof(i8) == 1 && sizeof(u8) == 1);
    static_assert(sizeof(i16) == 2 && sizeof(u16) == 2);
    static_assert(sizeof(i32) == 4 && sizeof(u32) == 4);
    static_assert(sizeof(i64) == 8 && sizeof(u64) == 8);
    static_assert(sizeof(f32) == 4 && sizeof(f64) == 8);
    static_assert(IsTriviallyCopyable<i32>);
    static_assert(IsTriviallyDestructible<i32>);
    static_assert(!IsTriviallyDestructible<TrackedObj>);

    TEST_CHECK(countl_zero(u32{0}) == 32);
    TEST_CHECK(countl_zero(u32{1}) == 31);
    TEST_CHECK(countl_zero(u64{0}) == 64);
    TEST_CHECK(countl_zero(u64{1} << 63) == 0);
    TEST_CHECK(countr_zero(u32{8}) == 3);
    TEST_CHECK(countr_zero(u64{1} << 40) == 40);
    TEST_CHECK(popcount(u32{0b10110100}) == 4);
    TEST_CHECK(popcount(u64{0xFFFFFFFFFFFFFFFFULL}) == 64);
    TEST_CHECK(bit_ceil(u64{1}) == 1);
    TEST_CHECK(bit_ceil(u64{5}) == 8);
    TEST_CHECK(bit_floor(u64{7}) == 4);
    TEST_CHECK(is_power_of_two(u64{64}));
    TEST_CHECK(!is_power_of_two(u64{0}));
    TEST_CHECK(!is_power_of_two(u64{65}));
    TEST_CHECK(byteswap(u32{0x12345678u}) == 0x78563412u);

    const u64 h1 = hash_bytes("hello", 5);
    const u64 h2 = hash_bytes("hello", 5);
    const u64 h3 = hash_bytes("world", 5);
    TEST_CHECK(h1 == h2 && h1 != h3);
}

void test_arena_and_memory() {
    Arena arena(4 * 1024 * 1024);
    TEST_CHECK(arena.used() == 0);

    // Verify in-place extension of the top allocation on the arena
    void* p1 = arena.alloc(64, 16);
    TEST_CHECK(p1 != nullptr);
    void* p1_grown = arena.realloc(p1, 64, 256, 16);
    TEST_CHECK(p1_grown == p1);
    TEST_CHECK(arena.used() == 256);

    {
        ArenaTemp temp = arena.begin_temp();
        Array<int> temp_arr(temp.allocator());
        for (int i = 0; i < 1000; ++i) temp_arr.push_back(i);
        TEST_CHECK(arena.used() > 256);
    }
    TEST_CHECK(arena.used() == 256);

    // Scratch arena conflict resolution
    ArenaTemp s1 = get_scratch();
    Arena* conflict = &s1.arena();
    ArenaTemp s2 = get_scratch(&conflict, 1);
    TEST_CHECK(&s1.arena() != &s2.arena());

    // Generational Pool<T>
    Pool<TrackedObj> pool;
    TrackedObj::live_count = 0;
    Handle<TrackedObj> h_a = pool.emplace(42);
    Handle<TrackedObj> h_b = pool.emplace(99);
    TEST_CHECK(pool.size() == 2);
    TEST_CHECK(TrackedObj::live_count == 2);
    TEST_CHECK(pool.get(h_a) != nullptr && pool.get(h_a)->value == 42);
    TEST_CHECK(pool.remove(h_a));
    TEST_CHECK(!pool.is_valid(h_a));
    TEST_CHECK(pool.get(h_a) == nullptr);
    Handle<TrackedObj> h_c = pool.emplace(77);
    TEST_CHECK(h_c.index == h_a.index && h_c.generation != h_a.generation);
    TEST_CHECK(!pool.is_valid(h_a));
    TEST_CHECK(pool.get(h_c)->value == 77);
    pool.clear();
    TEST_CHECK(TrackedObj::live_count == 0);
}

Result<int> helper_try(int x) {
    if (x < 0) return ErrorCode::InvalidArgument;
    return x * 2;
}

Result<int> helper_try_chain(int x) {
    KEEL_TRY_ASSIGN(int a, helper_try(x));
    KEEL_TRY_ASSIGN(int b, helper_try(a + 1));
    return a + b;
}

void test_vocabulary() {
    // Optional triviality and sentinel zero-size-overhead guarantees
    static_assert(IsTriviallyCopyable<Optional<int>>);
    static_assert(IsTriviallyDestructible<Optional<int>>);
    static_assert(sizeof(SentinelOptional<i32, -1>) == sizeof(i32));

    Optional<int> opt;
    TEST_CHECK(!opt.has_value());
    TEST_CHECK(opt.value_or(10) == 10);
    opt = 25;
    TEST_CHECK(opt.has_value() && *opt == 25);
    opt.reset();
    TEST_CHECK(!opt.has_value());

    TrackedObj::live_count = 0;
    {
        Optional<TrackedObj> tobj(TrackedObj(7));
        TEST_CHECK(TrackedObj::live_count == 1);
        TEST_CHECK(tobj->value == 7);
    }
    TEST_CHECK(TrackedObj::live_count == 0);

    SentinelOptional<i32, -1> sopt;
    TEST_CHECK(!sopt.has_value());
    sopt = 0;
    TEST_CHECK(sopt.has_value() && *sopt == 0);
    sopt.reset();
    TEST_CHECK(!sopt.has_value());

    // Result & KEEL_TRY_ASSIGN
    auto ok_res = helper_try_chain(5);
    TEST_CHECK(ok_res.is_ok() && *ok_res == 10 + 22);
    auto err_res = helper_try_chain(-1);
    TEST_CHECK(err_res.is_err() && err_res.error().code == ErrorCode::InvalidArgument);

    // StringRef operations
    StringRef sr = "  hello/world.txt  ";
    TEST_CHECK(sr.trim() == "hello/world.txt");
    StringRef clean = sr.trim();
    TEST_CHECK(clean.starts_with("hello"));
    TEST_CHECK(clean.ends_with(".txt"));
    TEST_CHECK(clean.contains("world"));
    TEST_CHECK(clean.find('/') == 5);
    TEST_CHECK(clean.rfind('.') == 11);
    auto sp = clean.split_once('/');
    TEST_CHECK(sp.found && sp.first == "hello" && sp.second == "world.txt");
    TEST_CHECK(StringRef("AbCd").equals_ignore_ascii_case("abcd"));

    // InplaceString<N> (zero-heap, Rule-of-Zero hybrid static/inline string)
    {
        static_assert(IsTriviallyCopyable<InplaceString<64>>);
        constexpr const char* kLiteral = "Static Command";
        constexpr InplaceString<64> s_lit(kLiteral);
        TEST_CHECK(s_lit.is_static());
        TEST_CHECK(s_lit.data() == kLiteral);
        TEST_CHECK(s_lit.size() == 14);
        TEST_CHECK(s_lit == "Static Command");

        InplaceString<64> s_inline(StringRef("Dynamic Value"));
        TEST_CHECK(!s_inline.is_static());
        TEST_CHECK(s_inline == "Dynamic Value");
        TEST_CHECK(std::strcmp(s_inline.c_str(), "Dynamic Value") == 0);

        // Rule of Zero copy must not alias source inline buffer
        InplaceString<64> s_copy = s_inline;
        TEST_CHECK(s_copy.data() != s_inline.data());
        s_inline = StringRef("Mutated");
        TEST_CHECK(s_copy == "Dynamic Value");
        TEST_CHECK(s_inline == "Mutated");

        InplaceString<32> s_fmt = format_inplace<32>("Ch %d: %.1f dB", 2, -6.5);
        TEST_CHECK(s_fmt == "Ch 2: -6.5 dB");
    }

    // FunctionRef & Function<Sig, N>
    int multiplier = 3;
    auto lam = [&](int v) { return v * multiplier; };
    FunctionRef<int(int)> fref(lam);
    TEST_CHECK(fref(7) == 21);

    Function<int(int), 32> fn([multiplier](int v) noexcept { return v + multiplier; });
    TEST_CHECK(static_cast<bool>(fn));
    TEST_CHECK(fn(10) == 13);
    Function<int(int), 32> fn_moved = keel::move(fn);
    TEST_CHECK(!static_cast<bool>(fn));
    TEST_CHECK(fn_moved(10) == 13);
}

void test_containers() {
    TrackedObj::live_count = 0;
    {
        Array<TrackedObj> arr;
        for (int i = 0; i < 20; ++i) arr.emplace_back(i);
        TEST_CHECK(arr.size() == 20);
        TEST_CHECK(TrackedObj::live_count == 20);

        Array<TrackedObj> cloned = arr.clone();
        TEST_CHECK(cloned.size() == 20 && cloned[15].value == 15);
        TEST_CHECK(TrackedObj::live_count == 40);

        Array<TrackedObj> moved = keel::move(arr);
        TEST_CHECK(arr.empty() && arr.data() == nullptr && arr.capacity() == 0);
        TEST_CHECK(moved.size() == 20);

        moved.remove_at(0);
        TEST_CHECK(moved.size() == 19 && moved[0].value == 1);
        moved.swap_remove(0);
        TEST_CHECK(moved.size() == 18 && moved[0].value == 19);
    }
    TEST_CHECK(TrackedObj::live_count == 0);

    // SmallArray & InplaceArray
    SmallArray<int, 4> small;
    for (int i = 0; i < 4; ++i) small.push_back(i * 10);
    TEST_CHECK(small.is_inline());
    // Self-aliasing push_back across inline-to-heap boundary
    small.push_back(small[0]);
    TEST_CHECK(!small.is_inline());
    TEST_CHECK(small.size() == 5 && small[4] == 0);
    SmallArray<int, 4> small_copy = small.clone();
    TEST_CHECK(small_copy.size() == 5 && small_copy[3] == 30);

    // HashMap & HashSet with heterogeneous StringRef lookup
    HashMap<String, int> map;
    TEST_CHECK(map.put(String::from("alpha"), 1));
    TEST_CHECK(map.put(String::from("beta"), 2));
    TEST_CHECK(!map.put(String::from("alpha"), 10)); // Overwrite existing

    // Lookup using StringRef without allocating a String!
    int* p_alpha = map.get(StringRef("alpha"));
    TEST_CHECK(p_alpha != nullptr && *p_alpha == 10);
    TEST_CHECK(map.contains("beta"));
    TEST_CHECK(!map.contains("gamma"));

    int& gamma_ref = map.get_or_insert(String::from("gamma"), 30);
    TEST_CHECK(gamma_ref == 30);
    gamma_ref = 33;
    TEST_CHECK(*map.get("gamma") == 33);

    HashMap<String, int> map_clone = map.clone();
    TEST_CHECK(map_clone.size() == 3 && *map_clone.get("gamma") == 33);

    TEST_CHECK(map.remove("beta"));
    TEST_CHECK(!map.contains("beta") && map.size() == 2);

    HashSet<int> set;
    for (int i = 0; i < 100; ++i) TEST_CHECK(set.insert(i));
    for (int i = 0; i < 100; ++i) TEST_CHECK(!set.insert(i));
    for (int i = 0; i < 50; ++i) TEST_CHECK(set.remove(i));
    TEST_CHECK(set.size() == 50);
    for (int i = 50; i < 100; ++i) TEST_CHECK(set.contains(i));

    // ArenaArray<T> triviality & in-place try_extend growth
    static_assert(IsTriviallyDestructible<ArenaArray<int>>);
    static_assert(!IsTriviallyDestructible<ArenaArray<TrackedObj>>);
    {
        Arena arena(1024 * 1024);
        ArenaArray<int> aarr(arena, 8);
        const int* init_ptr = aarr.data();
        for (int i = 0; i < 64; ++i) aarr.push_back(i * 3);
        TEST_CHECK(aarr.size() == 64 && aarr.data() == init_ptr && aarr[20] == 60);
    }

    // LruCache<K, V, Capacity>
    LruCache<int, int, 3> lru;
    lru.put(1, 10);
    lru.put(2, 20);
    lru.put(3, 30);
    TEST_CHECK(lru.size() == 3);
    TEST_CHECK(lru.find(1) != nullptr && *lru.find(1) == 10); // Promotes key 1; key 2 is now LRU
    lru.put(4, 40);                                           // Evicts key 2
    TEST_CHECK(lru.find(2) == nullptr);
    TEST_CHECK(lru.find(1) != nullptr && *lru.find(1) == 10);
    TEST_CHECK(lru.find(3) != nullptr && *lru.find(3) == 30);
    TEST_CHECK(lru.find(4) != nullptr && *lru.find(4) == 40);
}

void test_text_utf8_and_numbers() {
    String s = String::from("keel");
    TEST_CHECK(s.size() == 4 && std::strcmp(s.c_str(), "keel") == 0);
    s.append(" library");
    TEST_CHECK(s == "keel library");
    // Self-aliasing append
    s.append(s.substr(0, 4));
    TEST_CHECK(s == "keel librarykeel");

    // UTF-8 validation, decoding, encoding
    const StringRef utf8_sample = "Привет, мир! 🚀";
    TEST_CHECK(utf8_validate(utf8_sample));
    TEST_CHECK(utf8_codepoint_count(utf8_sample) == 14);

    // Overlong 2-byte sequence (0xC0 0x80) and UTF-16 surrogate (0xED 0xA0 0x80) must be rejected
    const char overlong[] = {'\xC0', '\x80', '\0'};
    const char surrogate[] = {'\xED', '\xA0', '\x80', '\0'};
    TEST_CHECK(!utf8_validate(StringRef(overlong, 2)));
    TEST_CHECK(!utf8_validate(StringRef(surrogate, 3)));

    // Integer parsing & formatting
    auto i_ok = parse_i64("-9223372036854775808");
    TEST_CHECK(i_ok.is_ok() && *i_ok == INT64_MIN);
    auto i_ovf = parse_i64("9223372036854775808");
    TEST_CHECK(i_ovf.is_err() && i_ovf.error().code == ErrorCode::Overflow);
    auto u_ok = parse_u64("18446744073709551615");
    TEST_CHECK(u_ok.is_ok() && *u_ok == UINT64_MAX);

    // Floating-point shortest round-trip vs std::to_chars / std::from_chars
    const f64 test_doubles[] = {
        0.0,
        -0.0,
        1.0,
        -1.0,
        0.1,
        0.2,
        0.3,
        0.1 + 0.2,
        3.14159265358979323846,
        2.71828182845904523536,
        1e-4,
        1e-5,
        1e15,
        1e16,
        1e20,
        1e-20,
        1.2345678901234567e-100,
        9.876543210987654e+150,
        2.2250738585072014e-308, // DBL_MIN (smallest normal)
        5e-324,                  // DBL_TRUE_MIN (smallest subnormal)
        1.7976931348623157e+308, // DBL_MAX
    };

    for (f64 d : test_doubles) {
        char buf[64];
        const isize len = format_f64(buf, sizeof(buf), d);
        TEST_CHECK(len > 0);
        auto parsed = parse_f64(StringRef(buf, len));
        TEST_CHECK(parsed.is_ok());
        if (parsed.is_ok()) {
            u64 b1 = 0, b2 = 0;
            std::memcpy(&b1, &d, 8);
            f64 pv = *parsed;
            std::memcpy(&b2, &pv, 8);
            TEST_CHECK(b1 == b2);

            // Also verify that std::from_chars parses our shortest output back to exact same double!
            f64 std_parsed = 0.0;
            auto [ptr, ec] = std::from_chars(buf, buf + len, std_parsed);
            TEST_CHECK(ec == std::errc{} && ptr == buf + len);
            u64 b_std = 0;
            std::memcpy(&b_std, &std_parsed, 8);
            TEST_CHECK(b1 == b_std);
        }
    }

    // Format API (`%S`, `%d`, `%.2f`)
    String formatted = format("Item %S: count=%d, ratio=%.2f%%", StringRef("Widget"), 42, 98.765);
    TEST_CHECK(formatted == "Item Widget: count=42, ratio=98.77%");
}

void test_fs_and_io() {
    TEST_CHECK(path_dirname("/usr/local/bin/keel") == "/usr/local/bin");
    TEST_CHECK(path_basename("/usr/local/bin/keel") == "keel");
    TEST_CHECK(path_extension("archive.tar.gz") == ".gz");
    TEST_CHECK(path_stem("archive.tar.gz") == "archive.tar");
    TEST_CHECK(path_extension(".gitignore") == "");
    TEST_CHECK(path_join("a/b/", "/c/d") == "/c/d");
    TEST_CHECK(path_join("a/b", "c/d") == "a/b/c/d");
    TEST_CHECK(path_normalize("a/b/../c/./d//e") == "a/c/d/e");

    auto tmp_res = fs_temp_dir();
    TEST_CHECK(tmp_res.is_ok());
    String test_dir = path_join(*tmp_res, "keel_unit_test_dir");
    (void)fs_create_dirs(test_dir);
    TEST_CHECK(fs_exists(test_dir));

    // Test UTF-8 filename (Cyrillic)
    String file_path = path_join(test_dir, "тест_файл.txt");
    const StringRef payload = "Line 1: Hello keel!\nLine 2: Проверка UTF-8 пути\n";
    TEST_CHECK(fs_write_text(file_path, payload).is_ok());
    TEST_CHECK(fs_exists(file_path));

    auto stat_res = fs_stat(file_path);
    TEST_CHECK(stat_res.is_ok() && stat_res->is_file && stat_res->size == static_cast<u64>(payload.size()));

    auto read_res = fs_read_text(file_path);
    TEST_CHECK(read_res.is_ok() && *read_res == payload);

    int dir_entries = 0;
    Error dir_err = fs_read_dir(test_dir, [&](const DirEntry& ent) {
        if (ent.name == "тест_файл.txt" && ent.is_file) {
            ++dir_entries;
        }
        return true;
    });
    TEST_CHECK(dir_err.is_ok() && dir_entries == 1);

    TEST_CHECK(fs_remove(file_path).is_ok());
    TEST_CHECK(!fs_exists(file_path));
    TEST_CHECK(fs_remove(test_dir).is_ok());
}

void test_std_interop() {
#if !defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS != 0
    std::string std_s = "from std::string";
    StringRef ref_from_std = std_s; // Zero-cost implicit conversion without interop_std.h
    TEST_CHECK(ref_from_std == "from std::string");

    std::string roundtrip = to_std_string(ref_from_std);
    TEST_CHECK(roundtrip == std_s);

    std::vector<int> std_vec = {10, 20, 30, 40};
    ArrayRef<int> arr_ref = std_vec;
    TEST_CHECK(arr_ref.size() == 4 && arr_ref[2] == 30);
#endif
}

void test_algo_and_rtree() {
    Array<int> nums;
    for (int i = 100; i >= 1; --i) {
        nums.push_back((i * 37) % 101);
    }
    keel::sort(nums);
    for (isize i = 1; i < nums.size(); ++i) {
        TEST_CHECK(nums[i - 1] <= nums[i]);
    }
    TEST_CHECK(keel::binary_search(ArrayRef<int>(nums), nums[42]));
    TEST_CHECK(!keel::binary_search(ArrayRef<int>(nums), 999999));

    RTree<u32, f32, 2, 4> tree;
    Array<RTreeItem<u32, f32, 2>> items;
    for (u32 y = 0; y < 10; ++y) {
        for (u32 x = 0; x < 10; ++x) {
            const u32 id = y * 10 + x;
            const f32 fx = static_cast<f32>(x * 10);
            const f32 fy = static_cast<f32>(y * 10);
            items.push_back(RTreeItem<u32, f32, 2>{
                .id = id,
                .box = RTreeBox<f32, 2>::from_xywh(fx, fy, 8.0f, 8.0f)
            });
        }
    }
    tree.build(items);
    TEST_CHECK(!tree.empty());
    TEST_CHECK(tree.item_count() == 100);

    Array<u32> hits;
    tree.query_box(RTreeBox<f32, 2>::from_xywh(12.0f, 22.0f, 15.0f, 15.0f), hits);
    // Should hit cells (1,2), (2,2), (1,3), (2,3) -> ids 21, 22, 31, 32
    keel::sort(hits);
    TEST_CHECK(hits.size() == 4);
    if (hits.size() == 4) {
        TEST_CHECK(hits[0] == 21 && hits[1] == 22 && hits[2] == 31 && hits[3] == 32);
    }

    int point_hits = 0;
    u32 hit_id = 999;
    tree.query_point(44.0f, 54.0f, [&](u32 id) {
        ++point_hits;
        hit_id = id;
    });
    TEST_CHECK(point_hits == 1 && hit_id == 54);

    // 3D RTree (Dim = 3)
    RTree<u32, f32, 3, 4> tree3d;
    Array<RTreeItem<u32, f32, 3>> items3d;
    for (u32 z = 0; z < 4; ++z) {
        for (u32 y = 0; y < 4; ++y) {
            for (u32 x = 0; x < 4; ++x) {
                const u32 id = z * 16 + y * 4 + x;
                items3d.push_back(RTreeItem<u32, f32, 3>{
                    .id = id,
                    .box = RTreeBox<f32, 3>::from_xyzwhd(
                        static_cast<f32>(x * 10),
                        static_cast<f32>(y * 10),
                        static_cast<f32>(z * 10),
                        6.0f, 6.0f, 6.0f
                    )
                });
            }
        }
    }
    tree3d.build(items3d);
    TEST_CHECK(tree3d.item_count() == 64);

    u32 hit3d = 999;
    int count3d = 0;
    tree3d.query_point(12.0f, 22.0f, 32.0f, [&](u32 id) {
        ++count3d;
        hit3d = id;
    });
    TEST_CHECK(count3d == 1 && hit3d == (3 * 16 + 2 * 4 + 1));

    // RingBuffer / Deque
    RingBuffer<int> rb;
    for (int i = 0; i < 20; ++i) {
        rb.push_back(i * 10);
    }
    TEST_CHECK(rb.size() == 20 && rb.front() == 0 && rb.back() == 190);
    for (int i = 0; i < 5; ++i) {
        rb.pop_front();
    }
    TEST_CHECK(rb.size() == 15 && rb.front() == 50);
    rb.push_front(40);
    rb.push_front(30);
    TEST_CHECK(rb.size() == 17 && rb.front() == 30 && rb[1] == 40 && rb[2] == 50);

    // UTF-8 boundary, word navigation & case-insensitive search
    const StringRef text = "Привет, Мир!";
    TEST_CHECK(utf8_next_boundary(text, 0) == 2);
    TEST_CHECK(utf8_prev_boundary(text, 2) == 0);
    TEST_CHECK(utf8_next_word_boundary(text, 0) == 12); // "Привет" = 6 Cyrillic chars = 12 bytes
    isize w_start = 0, w_end = 0;
    utf8_word_bounds_at(text, 4, w_start, w_end);
    TEST_CHECK(w_start == 0 && w_end == 12);
    isize m_pos = 0, m_len = 0;
    TEST_CHECK(utf8_find_case_insensitive(text, "МИР", m_pos, m_len));
    TEST_CHECK(m_pos == 14 && m_len == 6);
}

} // namespace

int main() {
    test_core_and_bits();
    test_arena_and_memory();
    test_vocabulary();
    test_containers();
    test_text_utf8_and_numbers();
    test_fs_and_io();
    test_std_interop();
    test_algo_and_rtree();

    std::printf("keel tests finished: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}
