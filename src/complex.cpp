#include "keel/complex.h"

#include <cmath>

namespace keel {

f32 abs(c32 z) noexcept {
    return std::hypot(z.re, z.im);
}

f64 abs(c64 z) noexcept {
    return std::hypot(z.re, z.im);
}

f32 arg(c32 z) noexcept {
    return std::atan2(z.im, z.re);
}

f64 arg(c64 z) noexcept {
    return std::atan2(z.im, z.re);
}

c32 polar(f32 r, f32 theta) noexcept {
    return {r * std::cos(theta), r * std::sin(theta)};
}

c64 polar(f64 r, f64 theta) noexcept {
    return {r * std::cos(theta), r * std::sin(theta)};
}

} // namespace keel
