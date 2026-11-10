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
    TEST_CHECK(pool.get(h_b) != nullptr && pool.get(h_b)->value == 99);
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

    // FixedString<N> (zero-heap, Rule-of-Zero pure stack string) & SmallString<N> (SSO + Allocator fallback)
    {
        static_assert(IsTriviallyCopyable<FixedString<64>>);
        constexpr FixedString<64> s_lit("Static Command");
        TEST_CHECK(s_lit.size() == 14);
        TEST_CHECK(s_lit == "Static Command");

        FixedString<64> s_fixed(StringRef("Dynamic Value"));
        TEST_CHECK(s_fixed == "Dynamic Value");
        TEST_CHECK(std::strcmp(s_fixed.c_str(), "Dynamic Value") == 0);

        // Rule of Zero copy must not alias source inline buffer
        FixedString<64> s_copy = s_fixed;
        TEST_CHECK(s_copy.data() != s_fixed.data());
        s_fixed = StringRef("Mutated");
        TEST_CHECK(s_copy == "Dynamic Value");
        TEST_CHECK(s_fixed == "Mutated");

        // Truncation at N - 1
        FixedString<8> s_trunc("0123456789");
        TEST_CHECK(s_trunc.size() == 7 && s_trunc.full() && s_trunc == "0123456");

        FixedString<32> s_fmt = format_fixed<32>("Ch %d: %.1f dB", 2, -6.5);
        TEST_CHECK(s_fmt == "Ch 2: -6.5 dB");

        // SmallString<N>: inline up to N - 1, spills to Allocator when size >= N
        SmallString<16> sm("short");
        TEST_CHECK(sm.is_inline() && sm.size() == 5 && sm == "short");
        SmallString<16> sm_moved_inline = keel::move(sm);
        TEST_CHECK(sm.empty() && sm_moved_inline.is_inline() && sm_moved_inline == "short");

        // Self-aliasing append across inline-to-heap boundary
        sm_moved_inline.append("_0123456789_overflow");
        TEST_CHECK(!sm_moved_inline.is_inline());
        TEST_CHECK(sm_moved_inline == "short_0123456789_overflow");
        sm_moved_inline.append(sm_moved_inline.substr(0, 5));
        TEST_CHECK(sm_moved_inline == "short_0123456789_overflowshort");

        SmallString<16> sm_cloned = sm_moved_inline.clone();
        TEST_CHECK(!sm_cloned.is_inline() && sm_cloned == sm_moved_inline);

        SmallString<32> sm_fmt = format_small<32>("Small {}: {}", "ok", 42);
        TEST_CHECK(sm_fmt.is_inline() && sm_fmt == "Small ok: 42");
    }

    // FunctionRef & FixedFunction<Sig, N>
    int multiplier = 3;
    auto lam = [&](int v) { return v * multiplier; };
    FunctionRef<int(int)> fref(lam);
    TEST_CHECK(fref(7) == 21);

    FixedFunction<int(int), 32> fn([multiplier](int v) noexcept { return v + multiplier; });
    TEST_CHECK(static_cast<bool>(fn));
    TEST_CHECK(fn(10) == 13);
    FixedFunction<int(int), 32> fn_moved = keel::move(fn);
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

    // SmallArray & FixedArray
    SmallArray<int, 4> small;
    for (int i = 0; i < 4; ++i) small.push_back(i * 10);
    TEST_CHECK(small.is_inline());
    // Self-aliasing push_back across inline-to-heap boundary
    small.push_back(small[0]);
    TEST_CHECK(!small.is_inline());
    TEST_CHECK(small.size() == 5 && small[4] == 0);
    SmallArray<int, 4> small_copy = small.clone();
    TEST_CHECK(small_copy.size() == 5 && small_copy[3] == 30);

    FixedArray<int, 4> fixed_arr;
    TEST_CHECK(fixed_arr.try_push_back(10) && fixed_arr.try_push_back(20));
    fixed_arr.push_back(30);
    fixed_arr.push_back(40);
    TEST_CHECK(fixed_arr.full() && !fixed_arr.try_push_back(50));
    TEST_CHECK(fixed_arr.front() == 10 && fixed_arr.back() == 40);

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
        const usize len = format_f64(buf, sizeof(buf), d);
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

    std::string_view sv_from_ref = ref_from_std; // Zero-header implicit conversion to std::string_view
    TEST_CHECK(sv_from_ref == "from std::string");

    String k_str = String::from("keel::String");
    std::string_view sv_from_str = k_str;
    TEST_CHECK(sv_from_str == "keel::String");

    FixedString<32> k_inp("keel::FixedString");
    std::string_view sv_from_inp = k_inp;
    TEST_CHECK(sv_from_inp == "keel::FixedString");

    SmallString<32> k_sm("keel::SmallString");
    std::string_view sv_from_sm = k_sm;
    TEST_CHECK(sv_from_sm == "keel::SmallString");

    std::string roundtrip = to_std_string(ref_from_std);
    TEST_CHECK(roundtrip == std_s);

    std::vector<int> std_vec = {10, 20, 30, 40};
    ArrayRef<int> arr_ref = std_vec;
    TEST_CHECK(arr_ref.size() == 4 && arr_ref[2] == 30);

    std::span<const int> span_from_ref = arr_ref;
    TEST_CHECK(span_from_ref.size() == 4 && span_from_ref[2] == 30);

    SmallArray<int, 4> small_arr;
    small_arr.push_back(7);
    small_arr.push_back(8);
    std::span<const int> span_from_small = small_arr;
    std::span<int> mut_span_from_small = small_arr;
    TEST_CHECK(span_from_small.size() == 2 && mut_span_from_small[1] == 8);
#endif
}

void test_algo_and_rtree() {
    Array<int> nums;
    for (int i = 100; i >= 1; --i) {
        nums.push_back((i * 37) % 101);
    }
    keel::sort(nums);
    for (usize i = 1; i < nums.size(); ++i) {
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

    // Ring, SmallRing, FixedRing
    Ring<int> rb;
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

    FixedRing<int, 3> fring;
    fring.push_back_overwrite(1);
    fring.push_back_overwrite(2);
    fring.push_back_overwrite(3);
    TEST_CHECK(fring.full() && fring.front() == 1 && fring.back() == 3);
    fring.push_back_overwrite(4);
    TEST_CHECK(fring.full() && fring.front() == 2 && fring[1] == 3 && fring.back() == 4);

    SmallRing<int, 4> sring;
    for (int i = 0; i < 4; ++i) sring.push_back(i + 1);
    TEST_CHECK(sring.is_inline() && sring.size() == 4);
    sring.pop_front();
    sring.push_back(5);
    TEST_CHECK(sring.is_inline() && sring.front() == 2 && sring.back() == 5);
    sring.push_back(6);
    TEST_CHECK(!sring.is_inline() && sring.size() == 5 && sring[0] == 2 && sring[4] == 6);

    // UTF-8 boundary, word navigation & case-insensitive search
    const StringRef text = "Привет, Мир!";
    TEST_CHECK(utf8_next_boundary(text, 0) == 2);
    TEST_CHECK(utf8_prev_boundary(text, 2) == 0);
    TEST_CHECK(utf8_next_word_boundary(text, 0) == 12); // "Привет" = 6 Cyrillic chars = 12 bytes
    usize w_start = 0, w_end = 0;
    utf8_word_bounds_at(text, 4, w_start, w_end);
    TEST_CHECK(w_start == 0 && w_end == 12);
    usize m_pos = 0, m_len = 0;
    TEST_CHECK(utf8_find_case_insensitive(text, "МИР", m_pos, m_len));
    TEST_CHECK(m_pos == 14 && m_len == 6);

    // Brace-style {} and zero-padded formatting + FNV-1a
    const auto fmt1 = format_fixed<64>("{:04d}-{:02d}-{:02d} {:.1f}", 2026, 11, 7, 12.34f);
    TEST_CHECK(fmt1.as_ref() == "2026-11-07 12.3");
    const auto fmt2 = format_fixed<64>("hex: {:04X}, str: {}", 0x2Au, "ok");
    TEST_CHECK(fmt2.as_ref() == "hex: 002A, str: ok" && format_fixed<64>("{:.3G}", 12340.0).as_ref() == "1.23E+04");
    const auto fmt_g1 = format_fixed<64>("{:g} %", 0.1 * 0.2);
    TEST_CHECK(fmt_g1.as_ref() == "0.02 %");
    const auto fmt_g2 = format_fixed<64>("{:g}", -2.0 + 3.0 * 0.2);
    TEST_CHECK(fmt_g2.as_ref() == "-1.4");
    const auto fmt_e1 = format_fixed<64>("{:.2e} / {:.3E}", 12340.0, 12000.0);
    TEST_CHECK(fmt_e1.as_ref() == "1.23e+04 / 1.200E+04");
    constexpr u64 h_compile = fnv1a64_chars("hello", 5);
    TEST_CHECK(h_compile == fnv1a64_bytes("hello", 5));

    // Verify format_arena allocates exact size (zero intermediate reallocation waste in target arena)
    // and avoids conflict when target arena is itself a thread-local scratch arena
    {
        Arena exact_arena;
        const StringRef long_sv = format_arena(
            exact_arena,
            "Long string exceeding initial 15-byte String capacity: [0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ] {}",
            42);
        TEST_CHECK(exact_arena.used() == long_sv.size() + 1);

        ArenaTemp scratch = get_scratch();
        const usize before = scratch.arena().used();
        const StringRef scratch_sv = format_arena(scratch.arena(), "Scratch-backed format: {} {:g}%", "ok", 12.5);
        TEST_CHECK(scratch.arena().used() == before + scratch_sv.size() + 1);
        TEST_CHECK(scratch_sv == "Scratch-backed format: ok 12.5%");

        // Verify path_normalize, fs_current_dir, and fs_exe_path do not rewind caller's scratch arena
        String norm_in_scratch = path_normalize("a/b/../c/./d/e/f", scratch.allocator());
        TEST_CHECK(scratch.arena().used() > before + scratch_sv.size() + 1);
        TEST_CHECK(norm_in_scratch.as_ref() == "a/c/d/e/f");

        auto cwd_res = fs_current_dir(scratch.allocator());
        TEST_CHECK(cwd_res.is_ok() && !cwd_res.value().empty());
        auto exe_res = fs_exe_path(scratch.allocator());
        TEST_CHECK(exe_res.is_ok() && !exe_res.value().empty());
        // Earlier allocations in `scratch` must remain untouched
        TEST_CHECK(norm_in_scratch.as_ref() == "a/c/d/e/f");
        TEST_CHECK(scratch_sv == "Scratch-backed format: ok 12.5%");
    }
}

void test_review_regressions() {
    // 1. bit_ceil overflow safety (no shift-by-64 UB)
    TEST_CHECK(bit_ceil((u64{1} << 63) + 1ULL) == 0ULL);
    TEST_CHECK(bit_ceil(u64{1} << 63) == (u64{1} << 63));

    // 2. format_f64_fixed large magnitudes (1e15), -0.0, carry propagation, and rounding
    TEST_CHECK(format_fixed<64>("%.6f", 1e15).as_ref() == "1000000000000000.000000");
    TEST_CHECK(format_fixed<64>("%.2f", -0.0).as_ref() == "-0.00");
    TEST_CHECK(format_fixed<64>("%.2f", 9.999).as_ref() == "10.00");
    TEST_CHECK(format_fixed<64>("%.2f", 0.009).as_ref() == "0.01");
    TEST_CHECK(format_fixed<64>("%.2f", -0.0001).as_ref() == "-0.00");

    // 3. Arena high alignment & ArenaTemp destructor rewind
    {
        Arena arena(256 * 1024);
        void* p1 = arena.alloc(3, 1);
        void* p64 = arena.alloc(64, 64);
        void* p256 = arena.alloc(128, 256);
        (void)p1;
        TEST_CHECK((reinterpret_cast<usize>(p64) & 63u) == 0);
        TEST_CHECK((reinterpret_cast<usize>(p256) & 255u) == 0);

        TrackedObj::live_count = 0;
        {
            ArenaTemp tmp = arena.begin_temp();
            TrackedObj* o1 = arena.create<TrackedObj>(10);
            MutArrayRef<TrackedObj> arr = arena.create_slice<TrackedObj>(3);
            TEST_CHECK(o1->value == 10 && arr.size() == 3);
            TEST_CHECK(TrackedObj::live_count == 4);
        }
        TEST_CHECK(TrackedObj::live_count == 0);
    }

    // 4. Self-aliasing on reallocation across Array, SmallArray, Ring, SmallRing, HashMap, Pool
    {
        Array<int> arr;
        for (int i = 0; i < 8; ++i) arr.push_back(i + 10);
        arr.emplace_back(arr[0]); // triggers capacity 8 -> 16 reallocation while reading arr[0]
        TEST_CHECK(arr.size() == 9 && arr[8] == 10);
        arr.append(arr); // self-append across reallocation
        TEST_CHECK(arr.size() == 18 && arr[9] == 10 && arr[17] == 10);
        arr.resize(40, arr[2]);
        TEST_CHECK(arr.size() == 40 && arr[39] == 12);

        SmallArray<int, 4> sarr;
        for (int i = 0; i < 4; ++i) sarr.push_back(i + 1);
        sarr.append(sarr); // inline -> heap self-append
        TEST_CHECK(sarr.size() == 8 && sarr[0] == 1 && sarr[4] == 1 && sarr[7] == 4);
        sarr.resize(20, sarr[3]);
        TEST_CHECK(sarr.size() == 20 && sarr[19] == 4);

        SmallRing<int, 2> sring;
        sring.push_back(11);
        sring.push_back(22);
        sring.emplace_back(sring[0]); // inline -> heap self-aliasing emplace_back
        sring.emplace_front(sring.back());
        TEST_CHECK(sring.size() == 4 && sring.front() == 11 && sring.back() == 11);

        Ring<int> ring;
        for (int i = 0; i < 8; ++i) ring.push_back(i + 100);
        ring.emplace_back(ring[0]); // 8 -> 16 grow reading ring[0]
        TEST_CHECK(ring.size() == 9 && ring.back() == 100);

        Pool<int> pool;
        Handle<int> first_h{};
        for (int i = 0; i < 8; ++i) {
            Handle<int> h = pool.emplace(i + 50);
            if (i == 0) first_h = h;
        }
        Handle<int> aliased_h = pool.emplace(*pool.get(first_h)); // 8 -> 16 grow reading slot 0
        TEST_CHECK(pool.get(aliased_h) != nullptr && *pool.get(aliased_h) == 50);
    }

    // 5. HashMap::clone with 0-arg .clone() value (FixedArray), move-only keel::sort, and move-only LruCache
    {
        HashMap<int, FixedArray<int, 4>> fmap;
        FixedArray<int, 4> fa;
        fa.push_back(7);
        fa.push_back(9);
        fmap.put(1, keel::move(fa));
        auto fmap_copy = fmap.clone();
        const auto* got = fmap_copy.get(1);
        TEST_CHECK(got != nullptr && got->size() == 2 && (*got)[0] == 7 && (*got)[1] == 9);

        Array<String> words;
        const char* raw[] = {"delta", "alpha", "charlie", "bravo", "echo", "foxtrot", "golf", "hotel", "india",
                             "juliet", "kilo", "lima", "mike", "november", "oscar", "papa", "quebec", "romeo"};
        for (const char* w : raw) words.push_back(String::from(w));
        keel::sort(words, [](const String& a, const String& b) { return a.as_ref() < b.as_ref(); });
        TEST_CHECK(words[0] == "alpha" && words[1] == "bravo" && words[words.size() - 1] == "romeo");

        LruCache<String, int, 3> lru;
        lru.put(String::from("one"), 1);
        lru.put(String::from("two"), 2);
        lru.put(String::from("three"), 3);
        TEST_CHECK(lru.get(StringRef("one")) != nullptr && *lru.get(StringRef("one")) == 1);
        lru.put(String::from("four"), 4); // evicts "two"
        TEST_CHECK(!lru.contains(StringRef("two")));
        TEST_CHECK(lru.peek(StringRef("one")) != nullptr && *lru.peek(StringRef("four")) == 4);
    }

    // 6. RTree degenerate zero-thickness point/segment boxes & high NodeCap traversal
    {
        RTree<u32, f32, 2, 64> rtree;
        Array<RTreeItem<u32, f32, 2>> items;
        // Zero-thickness point at (5, 5) and horizontal segment (0, 10)-(10, 10)
        items.push_back({100u, RTreeBox<f32, 2>::from_xywh(5.0f, 5.0f, 0.0f, 0.0f)});
        items.push_back({101u, RTreeBox<f32, 2>::from_xywh(0.0f, 10.0f, 10.0f, 0.0f)});
        for (u32 i = 0; i < 600; ++i) {
            const f32 x = static_cast<f32>(i % 30);
            const f32 y = static_cast<f32>(i / 30);
            items.push_back({i + 1000u, RTreeBox<f32, 2>::from_xywh(x, y, 0.5f, 0.5f)});
        }
        rtree.build(items);
        Array<u32> hits;
        rtree.query_box(RTreeBox<f32, 2>::from_xywh(5.0f, 5.0f, 0.0f, 0.0f), hits);
        bool found_point = false;
        for (u32 id : hits) {
            if (id == 100u) found_point = true;
        }
        TEST_CHECK(found_point);

        rtree.query_box(RTreeBox<f32, 2>::from_xywh(-10.0f, -10.0f, 110.0f, 110.0f), hits);
        TEST_CHECK(hits.size() == items.size());
    }

    // 7. Hybrid printf/brace formatter edge cases: JSON braces with printf, alignment, positional indices, format_into append
    {
        TEST_CHECK(format("{ \"id\": %d, \"ok\": %s }", 42, true) == "{ \"id\": 42, \"ok\": true }");
        TEST_CHECK(format("{1} -> {0:.2f}", 3.5, "gain") == "gain -> 3.50");
        TEST_CHECK(format("[{:>8.1f}] [{:<6}] [{:*^6}]", 12.34, "abc", "hi") == "[    12.3] [abc   ] [**hi**]");

        FixedString<32> fs("A=");
        format_into(fs, "{}", 10);
        format_into(fs, ", B={}", 20);
        TEST_CHECK(fs == "A=10, B=20");
    }
}

constexpr int constexpr_triple(int x) noexcept { return x * 3; }

int plain_increment(int x) { return x + 1; }

void test_four_tier_symmetry_and_purity() {
    // 1. Move-only invariants on ArenaArray<T>, Optional<String>, and Result<String>
    static_assert(IsTriviallyDestructible<ArenaArray<int>>);
    static_assert(!IsConstructible<ArenaArray<int>, const ArenaArray<int>&>);
    static_assert(!IsConstructible<Optional<String>, const Optional<String>&>);
    static_assert(!IsConstructible<Result<String>, const Result<String>&>);
    static_assert(IsTriviallyCopyable<Optional<int>>);
    static_assert(IsTriviallyCopyable<Result<int>>);

    // 2. Complete 4-tier Function family: FunctionRef, FixedFunction, SmallFunction, Function
    {
        constexpr FunctionRef<int(int)> c_ref(&constexpr_triple);
        static_assert(c_ref(7) == 21);
        TEST_CHECK(c_ref(9) == 27);

        // Free function & noexcept function pointer by lvalue (must not dangle or warn under -Wpedantic)
        auto make_fn_ref = [](int (*fp)(int) noexcept) -> FunctionRef<int(int)> {
            return FunctionRef<int(int)>(fp);
        };
        FunctionRef<int(int)> nx_ref = make_fn_ref(&constexpr_triple);
        TEST_CHECK(nx_ref && nx_ref(5) == 15);
        FunctionRef<int(int)> null_fp_ref = make_fn_ref(nullptr);
        TEST_CHECK(!null_fp_ref && null_fp_ref == nullptr);

        FunctionRef<int(int)> direct_free_ref(plain_increment);
        TEST_CHECK(direct_free_ref(41) == 42);

        // Empty owning Functions passed to FunctionRef become empty FunctionRef
        Function<int(int)> empty_dyn;
        SmallFunction<int(int), 16> empty_small;
        FixedFunction<int(int), 16> empty_fixed;
        TEST_CHECK(FunctionRef<int(int)>(empty_dyn).empty());
        TEST_CHECK(FunctionRef<int(int)>(empty_small).empty());
        TEST_CHECK(FunctionRef<int(int)>(empty_fixed).empty());

        // Move-only String capture in FixedFunction, SmallFunction, and Function
        FixedFunction<usize(), 64> f_fixed([s = String("fixed_move_only")]() noexcept { return s.size(); });
        TEST_CHECK(f_fixed() == 15);

        Arena fn_arena(256 * 1024);
        const usize mark0 = fn_arena.used();
        SmallFunction<int(int), 16> f_small([bias = 10](int x) noexcept { return x + bias; }, fn_arena.allocator());
        TEST_CHECK(f_small.is_inline() && fn_arena.used() == mark0);
        TEST_CHECK(f_small(5) == 15);

        // Spill SmallFunction (> 16 bytes) into fn_arena, preserving custom allocator across assignment!
        struct BigCapture {
            u64 a = 10, b = 20, c = 30, d = 40;
            int operator()(int x) const noexcept { return static_cast<int>(a + b + c + d) + x; }
        };
        f_small = BigCapture{};
        TEST_CHECK(!f_small.is_inline() && fn_arena.used() > mark0);
        TEST_CHECK(f_small(7) == 107);
        SmallFunction<int(int), 16> f_small_moved = keel::move(f_small);
        TEST_CHECK(f_small.empty() && !f_small_moved.is_inline() && f_small_moved(3) == 103);

        // Over-aligned capture in SmallFunction automatically spills with proper alignment
        struct alignas(64) OverAlignedCallable {
            int v = 99;
            int operator()() const noexcept {
                return ((reinterpret_cast<usize>(this) & 63u) == 0) ? v : -1;
            }
        };
        SmallFunction<int(), 64> f_aligned(OverAlignedCallable{}, fn_arena.allocator());
        TEST_CHECK(!f_aligned.is_inline() && f_aligned() == 99);

        // Pure 3-pointer Function<Sig> backed by Arena
        const usize mark_dyn = fn_arena.used();
        Function<StringRef()> f_dyn(fn_arena.allocator());
        TEST_CHECK(f_dyn.empty() && fn_arena.used() == mark_dyn);
        f_dyn = [msg = String("arena_fn", fn_arena.allocator())]() noexcept -> StringRef { return msg.as_ref(); };
        TEST_CHECK(!f_dyn.empty() && fn_arena.used() > mark_dyn && f_dyn() == "arena_fn");
    }

    // 3. String / SmallString / FixedString symmetry & UTF-8 codepoint-safe clamping
    {
        String s("  alpha_beta  ");
        TEST_CHECK(s.trim() == "alpha_beta");
        s.assign(s.trim()); // self-aliasing assign!
        TEST_CHECK(s == "alpha_beta" && s.front() == 'a' && s.back() == 'a');
        TEST_CHECK(s.starts_with("alpha") && s.ends_with("beta") && s.contains('_'));
        s.reserve(128);
        s.shrink_to_fit();
        TEST_CHECK(s.capacity() == s.size() && s == "alpha_beta");

        SmallString<16> sm("0123456789abcdef_heap");
        TEST_CHECK(!sm.is_inline());
        sm.assign("tiny");
        sm.shrink_to_fit();
        TEST_CHECK(sm.is_inline() && sm == "tiny" && sm.front() == 't' && sm.back() == 'y');

        // UTF-8 safe truncation on FixedString & format_fixed
        // "Привет" is 6 * 2 = 12 bytes; buffer_size = 6 => max_size = 5 bytes => clamps cleanly to 4 bytes ("Пр")
        FixedString<6> fs_cyr("Привет");
        TEST_CHECK(fs_cyr.size() == 4 && fs_cyr == "Пр" && utf8_validate(fs_cyr.as_ref()));

        // "Hi🚀" is 2 + 4 = 6 bytes; buffer_size = 6 => max_size = 5 bytes => clamps cleanly to 2 bytes ("Hi")
        FixedString<6> fs_emoji("Hi🚀");
        TEST_CHECK(fs_emoji.size() == 2 && fs_emoji == "Hi" && utf8_validate(fs_emoji.as_ref()));
        FixedString<7> fs_emoji_fit("Hi🚀");
        TEST_CHECK(fs_emoji_fit.size() == 6 && fs_emoji_fit == "Hi🚀");

        FixedString<6> fmt_utf8 = format_fixed<6>("Hi🚀");
        TEST_CHECK(fmt_utf8 == "Hi" && utf8_validate(fmt_utf8.as_ref()));

        // Explicit non-truncating try_* API on FixedString
        FixedString<8> fs_try;
        TEST_CHECK(fs_try.try_assign("12345"));
        TEST_CHECK(!fs_try.try_append("678")); // 5 + 3 > 7 -> rejected without partial mutation!
        TEST_CHECK(fs_try == "12345");
        TEST_CHECK(fs_try.try_append("67") && fs_try.full() && !fs_try.try_push_back('8'));
    }

    // 4. Nested move-only .clone() & shrink_to_fit across Array, SmallArray, FixedArray, Ring, ArenaArray, Optional
    {
        Array<String> names;
        names.push_back(String("charlie"));
        names.push_back(String("alpha"));
        names.push_back(String("bravo"));
        keel::sort(names); // Uses String::operator< directly!
        Array<String> names_copy = names.clone();
        TEST_CHECK(names_copy.size() == 3 && names_copy[0] == "alpha" && names_copy[2] == "charlie");

        SmallArray<String, 2> s_names;
        s_names.push_back(String("one"));
        s_names.push_back(String("two"));
        s_names.push_back(String("three"));
        TEST_CHECK(!s_names.is_inline());
        SmallArray<String, 2> s_names_copy = s_names.clone();
        TEST_CHECK(s_names_copy.size() == 3 && s_names_copy[2] == "three");
        s_names.pop_back();
        s_names.shrink_to_fit();
        TEST_CHECK(s_names.is_inline() && s_names.size() == 2 && s_names[1] == "two");

        const int init_vals[] = {10, 20, 30};
        FixedArray<int, 4> fa = FixedArray<int, 4>::from(init_vals);
        TEST_CHECK(fa.size() == 3 && !fa.try_append(init_vals) && fa.try_emplace_back(40) && fa.full());

        SmallRing<String, 2> sr;
        sr.push_back(String("r1"));
        sr.push_back(String("r2"));
        sr.push_back(String("r3"));
        TEST_CHECK(!sr.is_inline());
        SmallRing<String, 2> sr_copy = sr.clone();
        TEST_CHECK(sr_copy.size() == 3 && sr_copy.front() == "r1" && sr_copy.back() == "r3");
        sr.pop_front();
        sr.shrink_to_fit();
        TEST_CHECK(sr.is_inline() && sr.size() == 2 && sr.front() == "r2" && sr.back() == "r3");

        Arena a1(256 * 1024);
        Arena a2(256 * 1024);
        ArenaArray<String> aa(a1);
        aa.push_back(String("arena_s1", a1.allocator()));
        aa.push_back(String("arena_s2", a1.allocator()));
        ArenaArray<String> aa_cloned = aa.clone(&a2);
        TEST_CHECK(aa_cloned.size() == 2 && aa_cloned[0] == "arena_s1" && aa_cloned[1] == "arena_s2");
        TEST_CHECK(aa_cloned[0].allocator() == a2.allocator());

        Optional<String> opt_s(String("opt_owned"));
        Optional<String> opt_s_clone = opt_s.clone();
        TEST_CHECK(opt_s_clone.has_value() && *opt_s_clone == "opt_owned");
    }
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
    test_review_regressions();
    test_four_tier_symmetry_and_purity();

    std::printf("keel tests finished: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}
