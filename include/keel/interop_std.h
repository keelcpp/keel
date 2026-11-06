#pragma once

// Opt-in interoperability conversions to C++ standard library types.
// Note: StringRef and ArrayRef<T> already accept std::string, std::string_view, std::vector, and std::span
// implicitly WITHOUT including this header. Include `keel/interop_std.h` only when converting FROM keel TO std.

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
    return std::string(s.data(), static_cast<std::size_t>(s.size()));
}

[[nodiscard]] constexpr std::string_view to_std_string_view(StringRef s) noexcept {
    return std::string_view(s.data(), static_cast<std::size_t>(s.size()));
}

template <typename T> [[nodiscard]] constexpr std::span<const T> to_std_span(ArrayRef<T> arr) noexcept {
    return std::span<const T>(arr.data(), static_cast<std::size_t>(arr.size()));
}

template <typename T> [[nodiscard]] constexpr std::span<T> to_std_span(MutArrayRef<T> arr) noexcept {
    return std::span<T>(arr.data(), static_cast<std::size_t>(arr.size()));
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
