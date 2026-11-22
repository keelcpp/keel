#pragma once

#include "keel/core.h"
#include "keel/error.h"
#include "keel/ref.h"
#include "keel/result.h"

namespace keel {

// Fills `out` with cryptographically secure random bytes from the host OS CSPRNG
// (`getrandom` / `/dev/urandom` on Linux, `arc4random_buf` on macOS/BSD,
// `BCryptGenRandom` on Windows, `crypto.getRandomValues` on WebAssembly).
Error os_random_bytes(MutArrayRef<u8> out) noexcept;

// Returns a cryptographically secure random 64-bit unsigned integer from the host OS CSPRNG.
[[nodiscard]] Result<u64> os_random_u64() noexcept;

// Returns a cryptographically secure random 32-bit unsigned integer from the host OS CSPRNG.
[[nodiscard]] Result<u32> os_random_u32() noexcept;

} // namespace keel
