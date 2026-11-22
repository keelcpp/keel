#pragma once

#include "keel/core.h"
#include "keel/memory.h"
#include "keel/string.h"

namespace keel {

// Static compile-time target OS name ("Windows", "macOS", "Linux", "WebAssembly", "FreeBSD", "Unix", "Unknown").
[[nodiscard]] constexpr StringRef sys_os_name() noexcept {
#if defined(__EMSCRIPTEN__)
    return "WebAssembly";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#elif defined(__FreeBSD__)
    return "FreeBSD";
#elif defined(__unix__)
    return "Unix";
#else
    return "Unknown";
#endif
}

// Static compile-time CPU architecture ("x86_64", "arm64", "x86", "arm", "wasm32", "wasm64", "riscv64", "unknown").
[[nodiscard]] constexpr StringRef sys_cpu_arch() noexcept {
#if defined(__wasm64__)
    return "wasm64";
#elif defined(__wasm32__) || defined(__EMSCRIPTEN__)
    return "wasm32";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__riscv) && (__riscv_xlen == 64)
    return "riscv64";
#else
    return "unknown";
#endif
}

// Logical CPU hardware thread count (>= 1).
[[nodiscard]] u32 sys_cpu_count() noexcept;

// OS virtual memory page size in bytes (e.g. 4096 or 16384 on Apple Silicon; 65536 on WebAssembly).
[[nodiscard]] usize sys_page_size() noexcept;

// Total physical RAM installed in bytes (or WebAssembly linear memory capacity; 0 if unavailable).
[[nodiscard]] u64 sys_total_ram_bytes() noexcept;

// Currently available physical RAM in bytes (0 if unavailable).
[[nodiscard]] u64 sys_available_ram_bytes() noexcept;

// Human-readable OS / kernel version string (e.g. "6.8.0-generic", "10.0.22631", "14.5", "Web").
[[nodiscard]] String sys_os_version(Allocator* alloc = nullptr);

// Active user locale identifier (e.g. "en_US", "ru_RU", "en-US"; empty string if indeterminate).
[[nodiscard]] String sys_locale(Allocator* alloc = nullptr);

} // namespace keel
