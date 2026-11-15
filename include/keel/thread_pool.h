#pragma once

#include "keel/core.h"
#include "keel/function_ref.h"
#include "keel/memory.h"

namespace keel {

// Logical CPU hardware thread count (>= 1).
[[nodiscard]] u32 hardware_concurrency() noexcept;

// Persistent zero-allocation worker pool for data-parallel loops.
// The calling thread executes chunk 0 directly while N-1 background workers execute chunks 1..N-1,
// avoiding OS oversubscription and extra thread wakeups.
class ThreadPool {
public:
    // Process-wide singleton sized to `hardware_concurrency()` on first call.
    [[nodiscard]] static ThreadPool& global();

    // `num_workers == 0` selects `hardware_concurrency()`.
    // `num_workers == 1` spawns zero background threads and executes tasks inline.
    explicit ThreadPool(u32 num_workers = 0, Allocator* alloc = nullptr);
    ~ThreadPool() noexcept;

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    [[nodiscard]] u32 num_workers() const noexcept { return m_num_workers; }

    // Splits `[0, count)` evenly across active workers and blocks until all chunks finish.
    // `max_workers` (> 0) caps concurrency for memory-bandwidth-bound kernels (`0` = use all workers).
    // Nested calls from inside a running pool task execute inline without deadlocking.
    void run(usize count, FunctionRef<void(usize begin, usize end)> fn, u32 max_workers = 0);

private:
    struct Impl;
    Impl* m_impl = nullptr;
    Allocator* m_alloc = nullptr;
    u32 m_num_workers = 1;
};

// Executes `fn(begin, end)` across `[0, count)`:
// - Sequentially inline on the caller (`fn(0, count)`) when `count < min_threshold`, `max_workers == 1`,
//   or `ThreadPool::global().num_workers() <= 1`.
// - In parallel across `ThreadPool::global()` otherwise, with zero heap allocations.
template <typename F>
    requires requires(F& f, usize begin, usize end) { f(begin, end); }
inline void parallel_for(usize count, usize min_threshold, F&& fn, u32 max_workers = 0) {
    if (count == 0) return;
    if (count >= min_threshold && max_workers != 1) {
        ThreadPool& pool = ThreadPool::global();
        if (pool.num_workers() > 1) {
            pool.run(count, FunctionRef<void(usize, usize)>(fn), max_workers);
            return;
        }
    }
    fn(usize{0}, count);
}

} // namespace keel
