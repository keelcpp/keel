#pragma once

#include "keel/error.h"
#include "keel/optional.h"
#include "keel/string.h"

namespace keel {

// Reads environment variable `name`. Returns kNullOpt if unset (or invalid name).
// Distinguishes an unset variable (kNullOpt) from an explicitly empty value (String("")).
[[nodiscard]] Optional<String> env_get(StringRef name, Allocator* alloc = nullptr);

// Reads environment variable `name`, returning an empty String if unset.
[[nodiscard]] String env_get_or_empty(StringRef name, Allocator* alloc = nullptr);

// Returns true iff environment variable `name` is currently set (even if empty).
[[nodiscard]] bool env_has(StringRef name) noexcept;

// Sets environment variable `name` to `value` (overwriting any existing value).
// Rejects empty names or names containing '=' or '\0' with ErrorCode::InvalidArgument.
Error env_set(StringRef name, StringRef value) noexcept;

// Removes environment variable `name` (returns Ok if already unset).
// Rejects empty names or names containing '=' or '\0' with ErrorCode::InvalidArgument.
Error env_unset(StringRef name) noexcept;

} // namespace keel
