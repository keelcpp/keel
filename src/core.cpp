#include "keel/core.h"
#include "keel/error.h"
#include "keel/hash.h"
#include "keel/io.h"
#include "keel/number.h"

#include <cstdlib>

namespace keel {

namespace detail {

[[noreturn]] void assert_fail(const char* expr, const char* file, int line) noexcept {
    stderr_write("KEEL_ASSERT failed: ");
    stderr_write(expr ? expr : "<unknown>");
    stderr_write(" at ");
    stderr_write(file ? file : "<unknown>");
    stderr_write(":");
    char line_buf[32];
    const isize n = format_i64(line_buf, sizeof(line_buf), line);
    stderr_write(StringRef(line_buf, n));
    stderr_write("\n");
    stderr_flush();
    std::abort();
}

static inline u64 read_le64(const u8* p) noexcept {
    return static_cast<u64>(p[0]) | (static_cast<u64>(p[1]) << 8) | (static_cast<u64>(p[2]) << 16) |
           (static_cast<u64>(p[3]) << 24) | (static_cast<u64>(p[4]) << 32) | (static_cast<u64>(p[5]) << 40) |
           (static_cast<u64>(p[6]) << 48) | (static_cast<u64>(p[7]) << 56);
}

static inline u64 read_le32(const u8* p) noexcept {
    return static_cast<u64>(p[0]) | (static_cast<u64>(p[1]) << 8) | (static_cast<u64>(p[2]) << 16) |
           (static_cast<u64>(p[3]) << 24);
}

static inline u64 wymix(u64 a, u64 b) noexcept {
#if defined(__SIZEOF_INT128__)
    __uint128_t r = static_cast<__uint128_t>(a) * static_cast<__uint128_t>(b);
    return static_cast<u64>(r) ^ static_cast<u64>(r >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
    u64 hi = 0;
    u64 lo = 0;
    // Portable 64x64->128 multiply fallback without <intrin.h> header dependency
    const u64 a_lo = a & 0xFFFFFFFFULL;
    const u64 a_hi = a >> 32;
    const u64 b_lo = b & 0xFFFFFFFFULL;
    const u64 b_hi = b >> 32;
    const u64 p0 = a_lo * b_lo;
    const u64 p1 = a_lo * b_hi;
    const u64 p2 = a_hi * b_lo;
    const u64 p3 = a_hi * b_hi;
    const u64 cy = ((p0 >> 32) + (p1 & 0xFFFFFFFFULL) + (p2 & 0xFFFFFFFFULL)) >> 32;
    lo = p0 + (p1 << 32) + (p2 << 32);
    hi = p3 + (p1 >> 32) + (p2 >> 32) + cy;
    return lo ^ hi;
#else
    const u64 a_lo = a & 0xFFFFFFFFULL;
    const u64 a_hi = a >> 32;
    const u64 b_lo = b & 0xFFFFFFFFULL;
    const u64 b_hi = b >> 32;
    const u64 p0 = a_lo * b_lo;
    const u64 p1 = a_lo * b_hi;
    const u64 p2 = a_hi * b_lo;
    const u64 p3 = a_hi * b_hi;
    const u64 cy = ((p0 >> 32) + (p1 & 0xFFFFFFFFULL) + (p2 & 0xFFFFFFFFULL)) >> 32;
    const u64 lo = p0 + (p1 << 32) + (p2 << 32);
    const u64 hi = p3 + (p1 >> 32) + (p2 >> 32) + cy;
    return lo ^ hi;
#endif
}

} // namespace detail

u64 hash_bytes(const void* data, usize len, u64 seed) noexcept {
    static constexpr u64 kSecret0 = 0xa0761d6478bd642fULL;
    static constexpr u64 kSecret1 = 0xe7037ed1a0b428dbULL;
    static constexpr u64 kSecret2 = 0x8ebc6af09c88c6e3ULL;
    static constexpr u64 kSecret3 = 0x589965cc75374cc3ULL;

    const u8* p = static_cast<const u8*>(data);
    seed ^= detail::wymix(seed ^ kSecret0, kSecret1);
    u64 a = 0;
    u64 b = 0;
    if (len <= 16) {
        if (len >= 4) {
            a = (detail::read_le32(p) << 32) | detail::read_le32(p + ((len >> 3) << 2));
            b = (detail::read_le32(p + len - 4) << 32) | detail::read_le32(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = (static_cast<u64>(p[0]) << 16) | (static_cast<u64>(p[len >> 1]) << 8) | static_cast<u64>(p[len - 1]);
            b = 0;
        }
    } else {
        usize i = len;
        if (i > 48) {
            u64 see1 = seed;
            u64 see2 = seed;
            do {
                seed = detail::wymix(detail::read_le64(p) ^ kSecret1, detail::read_le64(p + 8) ^ seed);
                see1 = detail::wymix(detail::read_le64(p + 16) ^ kSecret2, detail::read_le64(p + 24) ^ see1);
                see2 = detail::wymix(detail::read_le64(p + 32) ^ kSecret3, detail::read_le64(p + 40) ^ see2);
                p += 48;
                i -= 48;
            } while (i > 48);
            seed ^= see1 ^ see2;
        }
        while (i > 16) {
            seed = detail::wymix(detail::read_le64(p) ^ kSecret1, detail::read_le64(p + 8) ^ seed);
            i -= 16;
            p += 16;
        }
        a = detail::read_le64(p + i - 16);
        b = detail::read_le64(p + i - 8);
    }
    a ^= kSecret1;
    b ^= seed;
    const u64 mixed = detail::wymix(a, b);
    return detail::wymix(kSecret0 ^ static_cast<u64>(len), mixed ^ kSecret1);
}

StringRef error_message(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Ok:
        return "ok";
    case ErrorCode::NotFound:
        return "not found";
    case ErrorCode::PermissionDenied:
        return "permission denied";
    case ErrorCode::AlreadyExists:
        return "already exists";
    case ErrorCode::InvalidArgument:
        return "invalid argument";
    case ErrorCode::InvalidData:
        return "invalid data";
    case ErrorCode::OutOfRange:
        return "out of range";
    case ErrorCode::Overflow:
        return "numeric overflow";
    case ErrorCode::IoError:
        return "I/O error";
    case ErrorCode::EndOfFile:
        return "unexpected end of file";
    case ErrorCode::WouldBlock:
        return "operation would block";
    case ErrorCode::NotSupported:
        return "operation not supported";
    case ErrorCode::Unknown:
    default:
        return "unknown error";
    }
}

} // namespace keel
