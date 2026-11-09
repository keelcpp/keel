#pragma once

// Opt-in interoperability conversions to C++ standard library types.
// Note: StringRef and ArrayRef<T> already convert implicitly to and from std::string_view and std::span
// WITHOUT including this header. Include `keel/interop_std.h` only when converting to std::string, std::vector,
// std::optional, or std::hash.

#include "keel/array.h"
#include "keel/optional.h"
#include "keel/ref.h"
#include "keel/string.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace keel {

[[nodiscard]] inline std::string to_std_string(StringRef s) {
    return std::string(s.data(), s.size());
}

[[nodiscard]] constexpr std::string_view to_std_string_view(StringRef s) noexcept {
    return std::string_view(s.data(), s.size());
}

template <typename T> [[nodiscard]] constexpr std::span<const T> to_std_span(ArrayRef<T> arr) noexcept {
    return std::span<const T>(arr.data(), arr.size());
}

template <typename T> [[nodiscard]] constexpr std::span<T> to_std_span(MutArrayRef<T> arr) noexcept {
    return std::span<T>(arr.data(), arr.size());
}

template <typename T> [[nodiscard]] std::vector<T> to_std_vector(ArrayRef<T> arr) {
    return std::vector<T>(arr.begin(), arr.end());
}

template <typename T> [[nodiscard]] constexpr std::optional<T> to_std_optional(const Optional<T>& opt) {
    if (opt.has_value()) return std::optional<T>(*opt);
    return std::nullopt;
}

template <typename T> [[nodiscard]] constexpr Optional<T> from_std_optional(const std::optional<T>& opt) {
    if (opt.has_value()) return Optional<T>(*opt);
    return kNullOpt;
}

} // namespace keel

template <> struct std::hash<keel::StringRef> {
    std::size_t operator()(keel::StringRef s) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::StringRef>{}(s));
    }
};

template <> struct std::hash<keel::String> {
    std::size_t operator()(const keel::String& s) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::String>{}(s));
    }
};

template <keel::usize N> struct std::hash<keel::SmallString<N>> {
    std::size_t operator()(const keel::SmallString<N>& s) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::SmallString<N>>{}(s));
    }
};

template <keel::usize N> struct std::hash<keel::FixedString<N>> {
    std::size_t operator()(const keel::FixedString<N>& s) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::FixedString<N>>{}(s));
    }
};
