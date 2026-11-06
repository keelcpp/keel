#include "keel/keel.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace keel;

namespace {

using Clock = std::chrono::high_resolution_clock;

template <typename Fn> double measure_ms(Fn&& fn, int iterations = 1) {
    const auto t0 = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        fn();
    }
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / iterations;
}

} // namespace

int main() {
    constexpr int kCount = 500000;

    // 1. Array<int> vs std::vector<int> push_back
    const double keel_arr_ms = measure_ms([&]() {
        Array<int> arr;
        for (int i = 0; i < kCount; ++i) arr.push_back(i);
        KEEL_ASSERT(arr.size() == kCount);
    }, 5);

    const double std_vec_ms = measure_ms([&]() {
        std::vector<int> vec;
        for (int i = 0; i < kCount; ++i) vec.push_back(i);
        KEEL_ASSERT(static_cast<int>(vec.size()) == kCount);
    }, 5);

    // 2. HashMap<i64, i64> vs std::unordered_map<i64, i64> insert + lookup
    const double keel_map_ms = measure_ms([&]() {
        HashMap<i64, i64> map;
        for (i64 i = 0; i < 200000; ++i) {
            map.put(i * 2654435761LL, i);
        }
        i64 sum = 0;
        for (i64 i = 0; i < 200000; ++i) {
            if (i64* v = map.get(i * 2654435761LL)) sum += *v;
        }
        KEEL_ASSERT(sum > 0);
    }, 3);

    const double std_map_ms = measure_ms([&]() {
        std::unordered_map<i64, i64> map;
        for (i64 i = 0; i < 200000; ++i) {
            map[i * 2654435761LL] = i;
        }
        i64 sum = 0;
        for (i64 i = 0; i < 200000; ++i) {
            auto it = map.find(i * 2654435761LL);
            if (it != map.end()) sum += it->second;
        }
        KEEL_ASSERT(sum > 0);
    }, 3);

    std::printf("=== keel vs std Benchmark ===\n");
    std::printf("Array<int> push_back (500k)           : %6.2f ms (std::vector: %6.2f ms)\n", keel_arr_ms, std_vec_ms);
    std::printf("HashMap<i64,i64> put+get (200k)       : %6.2f ms (std::unordered_map: %6.2f ms)\n", keel_map_ms,
                std_map_ms);
    return 0;
}
