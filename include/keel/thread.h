#pragma once

#include "keel/core.h"
#include "keel/error.h"
#include "keel/function.h"
#include "keel/memory.h"
#include "keel/result.h"
#include "keel/string.h"

namespace keel {

// Returns the OS thread identifier of the calling thread.
[[nodiscard]] u64 thread_id() noexcept;

// Hints the OS scheduler to yield the remainder of the current time slice.
void thread_yield() noexcept;

// Sets a human-readable debug name for the calling thread (visible in profilers/debuggers).
// Silently truncated to platform limits (e.g. 15 bytes on Linux) or no-op where unsupported.
void thread_set_name(StringRef name) noexcept;

// Move-only RAII OS thread. Automatically joins on destruction if still joinable (never calls std::terminate).
class Thread {
public:
    Thread() noexcept = default;
    ~Thread() noexcept {
        if (joinable()) {
            join();
        }
    }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(Thread&& other) noexcept : m_handle(other.m_handle), m_joinable(other.m_joinable) {
        other.m_handle = 0;
        other.m_joinable = false;
    }

    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (joinable()) {
                join();
            }
            m_handle = other.m_handle;
            m_joinable = other.m_joinable;
            other.m_handle = 0;
            other.m_joinable = false;
        }
        return *this;
    }

    // Spawns a new OS thread executing `fn`. If `name` is non-empty, sets the thread debug name on startup.
    [[nodiscard]] static Result<Thread> spawn(Function<void()> fn,
                                              StringRef name = {},
                                              Allocator* alloc = nullptr);

    [[nodiscard]] bool joinable() const noexcept { return m_joinable; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_joinable; }

    // Blocks until the thread finishes execution. No-op if not joinable.
    void join() noexcept;

    // Detaches the OS thread so its resources are reclaimed automatically upon exit.
    void detach() noexcept;

private:
    u64 m_handle = 0;
    bool m_joinable = false;
};

} // namespace keel
