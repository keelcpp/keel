#pragma once

// Opt-in interoperability conversions to C++ standard library types.
// Note: StringRef, ArrayRef<T>, and Complex<T> already convert implicitly to and from std::string_view, std::span,
// and std::complex<T> WITHOUT including this header. Include `keel/interop_std.h` only when converting to
// std::string, std::vector, std::optional, zero-copy std::complex spans/refs, or std::hash.

#include "keel/array.h"
#include "keel/complex.h"
#include "keel/memory.h"
#include "keel/optional.h"
#include "keel/ref.h"
#include "keel/string.h"

#include <complex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if __has_include(<memory_resource>) && (defined(__cpp_exceptions) || defined(__EXCEPTIONS) || (defined(_CPPUNWIND) && _CPPUNWIND != 0))
#include <memory_resource>
#define KEEL_INTEROP_HAS_PMR 1
#else
#define KEEL_INTEROP_HAS_PMR 0
#endif

namespace keel {

static_assert(sizeof(std::complex<f32>) == sizeof(c32) && alignof(std::complex<f32>) == alignof(c32));
static_assert(sizeof(std::complex<f64>) == sizeof(c64) && alignof(std::complex<f64>) == alignof(c64));

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

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr std::complex<T> to_std_complex(Complex<T> z) noexcept {
    return std::complex<T>(z.re, z.im);
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] constexpr Complex<T> from_std_complex(std::complex<T> z) noexcept {
    return Complex<T>(z.real(), z.imag());
}

// ISO C++ ([complex.numbers.general]) guarantees `std::complex<T>` has exact array layout `T[2]`.
template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline ArrayRef<Complex<T>> as_complex_ref(const std::complex<T>* data, usize size) noexcept {
    return ArrayRef<Complex<T>>(reinterpret_cast<const Complex<T>*>(data), size);
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline ArrayRef<Complex<T>> as_complex_ref(std::span<const std::complex<T>> s) noexcept {
    return ArrayRef<Complex<T>>(reinterpret_cast<const Complex<T>*>(s.data()), s.size());
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline MutArrayRef<Complex<T>> as_mut_complex_ref(std::complex<T>* data, usize size) noexcept {
    return MutArrayRef<Complex<T>>(reinterpret_cast<Complex<T>*>(data), size);
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline MutArrayRef<Complex<T>> as_mut_complex_ref(std::span<std::complex<T>> s) noexcept {
    return MutArrayRef<Complex<T>>(reinterpret_cast<Complex<T>*>(s.data()), s.size());
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline std::span<const std::complex<T>> to_std_complex_span(ArrayRef<Complex<T>> s) noexcept {
    return std::span<const std::complex<T>>(reinterpret_cast<const std::complex<T>*>(s.data()), s.size());
}

template <typename T>
    requires IsFloatingPoint<T>
[[nodiscard]] inline std::span<std::complex<T>> to_std_complex_span(MutArrayRef<Complex<T>> s) noexcept {
    return std::span<std::complex<T>>(reinterpret_cast<std::complex<T>*>(s.data()), s.size());
}

#if KEEL_INTEROP_HAS_PMR

// Adapts a `std::pmr::memory_resource*` to a `keel::Allocator`.
class PmrAllocatorBridge {
public:
    explicit PmrAllocatorBridge(std::pmr::memory_resource* mr = nullptr) noexcept
        : m_mr(mr ? mr : std::pmr::get_default_resource()) {
        m_alloc.ctx = m_mr;
        m_alloc.alloc = [](void* ctx, usize size, usize align) noexcept -> void* {
            if (size == 0) return nullptr;
            try {
                return static_cast<std::pmr::memory_resource*>(ctx)->allocate(size, align);
            } catch (...) {
                return nullptr;
            }
        };
        m_alloc.realloc = [](void* ctx, void* ptr, usize old_size, usize new_size, usize align) noexcept -> void* {
            auto* r = static_cast<std::pmr::memory_resource*>(ctx);
            if (new_size == 0) {
                if (ptr) r->deallocate(ptr, old_size, align);
                return nullptr;
            }
            void* next = nullptr;
            try {
                next = r->allocate(new_size, align);
            } catch (...) {
                return nullptr;
            }
            if (ptr && old_size > 0) {
                mem_copy(next, ptr, min(old_size, new_size));
                r->deallocate(ptr, old_size, align);
            }
            return next;
        };
        m_alloc.free = [](void* ctx, void* ptr, usize size, usize align) noexcept {
            if (ptr) {
                static_cast<std::pmr::memory_resource*>(ctx)->deallocate(ptr, size, align);
            }
        };
    }

    PmrAllocatorBridge(const PmrAllocatorBridge&) = delete;
    PmrAllocatorBridge& operator=(const PmrAllocatorBridge&) = delete;

    [[nodiscard]] Allocator* allocator() noexcept { return &m_alloc; }
    [[nodiscard]] std::pmr::memory_resource* resource() const noexcept { return m_mr; }

private:
    std::pmr::memory_resource* m_mr = nullptr;
    Allocator m_alloc{};
};

// Adapts a `keel::Allocator*` to a `std::pmr::memory_resource`.
class AllocatorMemoryResource final : public std::pmr::memory_resource {
public:
    explicit AllocatorMemoryResource(Allocator* alloc = nullptr) noexcept
        : m_alloc(alloc ? alloc : default_allocator()) {}

    [[nodiscard]] Allocator* allocator() const noexcept { return m_alloc; }

protected:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (bytes == 0) bytes = 1;
        void* p = m_alloc->try_alloc_bytes(bytes, alignment);
        if (!p) {
            throw std::bad_alloc();
        }
        return p;
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) noexcept override {
        if (p) {
            m_alloc->free_bytes(p, bytes, alignment);
        }
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

private:
    Allocator* m_alloc = nullptr;
};

#endif // KEEL_INTEROP_HAS_PMR

} // namespace keel

template <typename T>
    requires keel::IsFloatingPoint<T>
struct std::hash<keel::Complex<T>> {
    std::size_t operator()(keel::Complex<T> z) const noexcept {
        return static_cast<std::size_t>(keel::Hash<keel::Complex<T>>{}(z));
    }
};

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
