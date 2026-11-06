#include "keel/number.h"
#include "keel/bits.h"
#include "keel/memory.h"

#include <cmath>
#include <cstring>

namespace keel {

namespace {

constexpr int char_to_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    return -1;
}

struct U128 {
    u64 hi = 0;
    u64 lo = 0;
};

constexpr U128 mul64x64(u64 a, u64 b) noexcept {
#if defined(__SIZEOF_INT128__)
    __uint128_t p = static_cast<__uint128_t>(a) * static_cast<__uint128_t>(b);
    return U128{static_cast<u64>(p >> 64), static_cast<u64>(p)};
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
    return U128{hi, lo};
#endif
}

// Normalized 128-bit floating point: value = (sig.hi * 2^64 + sig.lo) * 2^(exp2 - 127), with MSB of sig.hi == 1.
struct Fp128 {
    u64 hi = 0;
    u64 lo = 0;
    i32 exp2 = 0;
};

constexpr Fp128 fp128_mul(Fp128 a, Fp128 b) noexcept {
    const U128 hh = mul64x64(a.hi, b.hi);
    const U128 hl = mul64x64(a.hi, b.lo);
    const U128 lh = mul64x64(a.lo, b.hi);

    u64 mid = (hl.lo >> 63) + (lh.lo >> 63);
    u64 lo = hh.lo + hl.hi;
    u64 carry = (lo < hh.lo) ? 1ULL : 0ULL;
    lo += lh.hi;
    if (lo < lh.hi) ++carry;
    lo += mid;
    if (lo < mid) ++carry;
    u64 hi = hh.hi + carry;
    i32 exp2 = a.exp2 + b.exp2;

    if ((hi & (1ULL << 63)) == 0) {
        hi = (hi << 1) | (lo >> 63);
        lo <<= 1;
        --exp2;
    }
    return Fp128{hi, lo, exp2 + 1};
}

// Computes normalized 128-bit approximation of 10^exp10 for exp10 in [-348, 348].
Fp128 compute_pow10(i32 exp10) noexcept {
    static constexpr Fp128 kOne = {0x8000000000000000ULL, 0ULL, 0};
    static constexpr Fp128 kTen = {0xA000000000000000ULL, 0ULL, 3};          // 10 = 1.25 * 2^3
    static constexpr Fp128 kTenth = {0xCCCCCCCCCCCCCCCCULL, 0xCCCCCCCCCCCCCCCDULL, -4}; // 0.1 = 0.8 * 2^-3

    if (exp10 == 0) return kOne;
    Fp128 base = (exp10 > 0) ? kTen : kTenth;
    u32 e = static_cast<u32>(exp10 > 0 ? exp10 : -exp10);
    Fp128 result = kOne;
    while (e > 0) {
        if ((e & 1u) != 0) {
            result = fp128_mul(result, base);
        }
        base = fp128_mul(base, base);
        e >>= 1u;
    }
    return result;
}

constexpr f64 kExactPow10[] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

u64 f64_to_bits(f64 v) noexcept {
    u64 bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

f64 bits_to_f64(u64 bits) noexcept {
    f64 v = 0.0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

u32 f32_to_bits(f32 v) noexcept {
    u32 bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

f32 bits_to_f32(u32 bits) noexcept {
    f32 v = 0.0f;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

f64 mantissa_exp10_to_f64(u64 mantissa, i32 exp10, bool negative, bool truncated) noexcept {
    if (mantissa == 0) {
        return negative ? -0.0 : 0.0;
    }

    // Clinger fast path when both mantissa and 10^|exp10| are exact in IEEE-754 double (53 bits)
    if (!truncated && (mantissa >> 53) == 0) {
        if (exp10 >= 0 && exp10 <= 22) {
            const f64 v = static_cast<f64>(mantissa) * kExactPow10[exp10];
            return negative ? -v : v;
        }
        if (exp10 < 0 && -exp10 <= 22) {
            const f64 v = static_cast<f64>(mantissa) / kExactPow10[-exp10];
            return negative ? -v : v;
        }
    }

    if (exp10 > 309) {
        return negative ? -HUGE_VAL : HUGE_VAL;
    }
    if (exp10 < -345) {
        return negative ? -0.0 : 0.0;
    }

    // Normalize 64-bit mantissa so MSB is bit 63
    const int lz = countl_zero(mantissa);
    const u64 norm_m = mantissa << lz;
    const Fp128 m_fp{norm_m, truncated ? 1ULL : 0ULL, 63 - lz};
    const Fp128 p10 = compute_pow10(exp10);
    const Fp128 prod = fp128_mul(m_fp, p10);

    // prod has MSB at bit 63 of prod.hi, representing 1.xxx * 2^prod.exp2
    i32 ieee_exp = prod.exp2 + 1023;
    if (ieee_exp >= 2047) {
        return negative ? -HUGE_VAL : HUGE_VAL;
    }

    u64 sig53 = 0;
    if (ieee_exp <= 0) {
        // Subnormal or underflow
        const i32 shift = 1 - ieee_exp + 11;
        if (shift >= 64) {
            return negative ? -0.0 : 0.0;
        }
        const u64 shifted = prod.hi >> shift;
        const u64 rem_mask = (1ULL << shift) - 1ULL;
        const u64 half = 1ULL << (shift - 1);
        const u64 rem = prod.hi & rem_mask;
        sig53 = shifted;
        if (rem > half || (rem == half && (prod.lo != 0 || (sig53 & 1ULL) != 0))) {
            ++sig53;
        }
        if (sig53 >= (1ULL << 52)) {
            ieee_exp = 1;
            sig53 &= ((1ULL << 52) - 1ULL);
        } else {
            ieee_exp = 0;
        }
    } else {
        // Normal number: extract top 53 bits from prod.hi (bits 63..11), round using bit 10..0 and prod.lo
        sig53 = prod.hi >> 11;
        const u64 rem = prod.hi & 0x7FFULL;
        if (rem > 0x400ULL || (rem == 0x400ULL && (prod.lo != 0 || (sig53 & 1ULL) != 0))) {
            ++sig53;
            if (sig53 >= (1ULL << 53)) {
                sig53 >>= 1;
                ++ieee_exp;
                if (ieee_exp >= 2047) {
                    return negative ? -HUGE_VAL : HUGE_VAL;
                }
            }
        }
        sig53 &= ((1ULL << 52) - 1ULL);
    }

    const u64 sign_bit = negative ? (1ULL << 63) : 0ULL;
    const u64 bits = sign_bit | (static_cast<u64>(ieee_exp) << 52) | sig53;
    return bits_to_f64(bits);
}

} // namespace

Result<i64, ParseError> parse_i64(StringRef str, int base) noexcept {
    if (base < 2 || base > 36) return ParseError{ErrorCode::InvalidArgument, 0};
    if (str.empty()) return ParseError{ErrorCode::InvalidData, 0};

    isize i = 0;
    bool negative = false;
    if (str[0] == '-') {
        negative = true;
        ++i;
    } else if (str[0] == '+') {
        ++i;
    }
    if (i >= str.size()) return ParseError{ErrorCode::InvalidData, i};

    const u64 u_base = static_cast<u64>(base);
    const u64 max_abs = negative ? (static_cast<u64>(INT64_MAX) + 1ULL) : static_cast<u64>(INT64_MAX);
    const u64 cutoff = max_abs / u_base;
    const u64 cutlim = max_abs % u_base;

    u64 acc = 0;
    for (; i < str.size(); ++i) {
        const int d = char_to_digit(str[i]);
        if (d < 0 || d >= base) return ParseError{ErrorCode::InvalidData, i};
        const u64 ud = static_cast<u64>(d);
        if (acc > cutoff || (acc == cutoff && ud > cutlim)) {
            return ParseError{ErrorCode::Overflow, i};
        }
        acc = acc * u_base + ud;
    }

    if (negative) {
        if (acc == static_cast<u64>(INT64_MAX) + 1ULL) return INT64_MIN;
        return -static_cast<i64>(acc);
    }
    return static_cast<i64>(acc);
}

Result<u64, ParseError> parse_u64(StringRef str, int base) noexcept {
    if (base < 2 || base > 36) return ParseError{ErrorCode::InvalidArgument, 0};
    if (str.empty()) return ParseError{ErrorCode::InvalidData, 0};

    isize i = 0;
    if (str[0] == '+') {
        ++i;
    } else if (str[0] == '-') {
        return ParseError{ErrorCode::InvalidData, 0};
    }
    if (i >= str.size()) return ParseError{ErrorCode::InvalidData, i};

    const u64 u_base = static_cast<u64>(base);
    const u64 cutoff = UINT64_MAX / u_base;
    const u64 cutlim = UINT64_MAX % u_base;

    u64 acc = 0;
    for (; i < str.size(); ++i) {
        const int d = char_to_digit(str[i]);
        if (d < 0 || d >= base) return ParseError{ErrorCode::InvalidData, i};
        const u64 ud = static_cast<u64>(d);
        if (acc > cutoff || (acc == cutoff && ud > cutlim)) {
            return ParseError{ErrorCode::Overflow, i};
        }
        acc = acc * u_base + ud;
    }
    return acc;
}

Result<f64, ParseError> parse_f64(StringRef str) noexcept {
    if (str.empty()) return ParseError{ErrorCode::InvalidData, 0};

    isize i = 0;
    bool negative = false;
    if (str[0] == '-') {
        negative = true;
        ++i;
    } else if (str[0] == '+') {
        ++i;
    }
    if (i >= str.size()) return ParseError{ErrorCode::InvalidData, i};

    const StringRef rem = str.substr(i);
    if (rem.equals_ignore_ascii_case("inf") || rem.equals_ignore_ascii_case("infinity")) {
        return negative ? -HUGE_VAL : HUGE_VAL;
    }
    if (rem.equals_ignore_ascii_case("nan")) {
        return static_cast<f64>(NAN);
    }

    u64 mantissa = 0;
    i32 exp10 = 0;
    int sig_digits = 0;
    bool any_digits = false;
    bool seen_dot = false;
    bool truncated = false;

    for (; i < str.size(); ++i) {
        const char c = str[i];
        if (c >= '0' && c <= '9') {
            any_digits = true;
            const u32 d = static_cast<u32>(c - '0');
            if (sig_digits == 0 && d == 0) {
                if (seen_dot) --exp10;
                continue;
            }
            if (sig_digits < 19) {
                mantissa = mantissa * 10ULL + d;
                ++sig_digits;
                if (seen_dot) --exp10;
            } else {
                if (!seen_dot) ++exp10;
                if (d != 0) truncated = true;
            }
        } else if (c == '.' && !seen_dot) {
            seen_dot = true;
        } else {
            break;
        }
    }

    if (!any_digits) return ParseError{ErrorCode::InvalidData, i};

    if (i < str.size() && (str[i] == 'e' || str[i] == 'E')) {
        ++i;
        if (i >= str.size()) return ParseError{ErrorCode::InvalidData, i};
        bool exp_neg = false;
        if (str[i] == '-') {
            exp_neg = true;
            ++i;
        } else if (str[i] == '+') {
            ++i;
        }
        if (i >= str.size()) return ParseError{ErrorCode::InvalidData, i};
        i32 parsed_exp = 0;
        for (; i < str.size(); ++i) {
            const char c = str[i];
            if (c < '0' || c > '9') return ParseError{ErrorCode::InvalidData, i};
            if (parsed_exp < 10000) {
                parsed_exp = parsed_exp * 10 + (c - '0');
            }
        }
        exp10 += exp_neg ? -parsed_exp : parsed_exp;
    }

    if (i != str.size()) {
        return ParseError{ErrorCode::InvalidData, i};
    }

    f64 val = mantissa_exp10_to_f64(mantissa, exp10, negative, truncated);
    if (std::isinf(val) && mantissa != 0) {
        return ParseError{ErrorCode::Overflow, str.size()};
    }
    return val;
}

Result<f32, ParseError> parse_f32(StringRef str) noexcept {
    auto res = parse_f64(str);
    if (!res.is_ok()) return res.error();
    const f64 d = res.value();
    if (std::isnan(d)) return static_cast<f32>(NAN);
    const f32 f = static_cast<f32>(d);
    if (std::isinf(f) && !std::isinf(d)) {
        return ParseError{ErrorCode::Overflow, str.size()};
    }
    return f;
}

isize format_u64(char* buf, isize cap, u64 value, int base, bool uppercase) noexcept {
    if (cap <= 0 || base < 2 || base > 36) return 0;
    const char* digits = uppercase ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" : "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[68];
    isize n = 0;
    const u64 u_base = static_cast<u64>(base);
    do {
        tmp[n++] = digits[value % u_base];
        value /= u_base;
    } while (value != 0);

    const isize out_len = min(n, cap);
    for (isize i = 0; i < out_len; ++i) {
        buf[i] = tmp[n - 1 - i];
    }
    return out_len;
}

isize format_i64(char* buf, isize cap, i64 value, int base, bool uppercase) noexcept {
    if (cap <= 0) return 0;
    if (value < 0 && base == 10) {
        buf[0] = '-';
        const u64 u_val = (value == INT64_MIN) ? (static_cast<u64>(INT64_MAX) + 1ULL) : static_cast<u64>(-value);
        return 1 + format_u64(buf + 1, cap - 1, u_val, base, uppercase);
    }
    return format_u64(buf, cap, static_cast<u64>(value), base, uppercase);
}

namespace {

int count_decimal_digits(u64 v) noexcept {
    int n = 1;
    while (v >= 10ULL) {
        v /= 10ULL;
        ++n;
    }
    return n;
}

isize emit_decimal_sci_or_fixed(char* buf, isize cap, bool negative, u64 digits_val, int num_digits,
                                i32 exp10) noexcept {
    char dig[24];
    for (int i = num_digits - 1; i >= 0; --i) {
        dig[i] = static_cast<char>('0' + (digits_val % 10ULL));
        digits_val /= 10ULL;
    }

    // Remove trailing zeros from `dig` and adjust `exp10`
    while (num_digits > 1 && dig[num_digits - 1] == '0') {
        --num_digits;
        ++exp10;
    }

    const i32 sci_exp = exp10 + num_digits - 1;
    char out[64];
    isize pos = 0;
    if (negative) out[pos++] = '-';

    // Choose fixed vs scientific notation (matching std::to_chars shortest general format)
    if (sci_exp >= -4 && sci_exp < 16) {
        if (exp10 >= 0) {
            for (int i = 0; i < num_digits; ++i) out[pos++] = dig[i];
            for (int i = 0; i < exp10; ++i) out[pos++] = '0';
        } else if (sci_exp >= 0) {
            const int int_digits = sci_exp + 1;
            for (int i = 0; i < int_digits; ++i) out[pos++] = dig[i];
            out[pos++] = '.';
            for (int i = int_digits; i < num_digits; ++i) out[pos++] = dig[i];
        } else {
            out[pos++] = '0';
            out[pos++] = '.';
            for (int i = 0; i < -sci_exp - 1; ++i) out[pos++] = '0';
            for (int i = 0; i < num_digits; ++i) out[pos++] = dig[i];
        }
    } else {
        out[pos++] = dig[0];
        if (num_digits > 1) {
            out[pos++] = '.';
            for (int i = 1; i < num_digits; ++i) out[pos++] = dig[i];
        }
        out[pos++] = 'e';
        if (sci_exp >= 0) {
            out[pos++] = '+';
        } else {
            out[pos++] = '-';
        }
        u32 abs_exp = static_cast<u32>(sci_exp >= 0 ? sci_exp : -sci_exp);
        if (abs_exp < 10) {
            out[pos++] = '0';
            out[pos++] = static_cast<char>('0' + abs_exp);
        } else {
            pos += format_u64(out + pos, static_cast<isize>(sizeof(out)) - pos, abs_exp, 10, false);
        }
    }

    const isize copy_len = min(pos, cap);
    mem_copy(buf, out, static_cast<usize>(copy_len));
    return copy_len;
}

} // namespace

isize format_f64(char* buf, isize cap, f64 value) noexcept {
    if (cap <= 0) return 0;

    const u64 bits = f64_to_bits(value);
    const bool negative = (bits >> 63) != 0;
    const u64 ieee_exp = (bits >> 52) & 0x7FFULL;
    const u64 ieee_mant = bits & ((1ULL << 52) - 1ULL);

    if (ieee_exp == 0x7FFULL) {
        const StringRef s = (ieee_mant != 0) ? StringRef("nan") : (negative ? StringRef("-inf") : StringRef("inf"));
        const isize n = min(s.size(), cap);
        mem_copy(buf, s.data(), static_cast<usize>(n));
        return n;
    }

    if (ieee_exp == 0 && ieee_mant == 0) {
        const StringRef s = negative ? StringRef("-0") : StringRef("0");
        const isize n = min(s.size(), cap);
        mem_copy(buf, s.data(), static_cast<usize>(n));
        return n;
    }

    const f64 abs_val = negative ? -value : value;

    // Estimate base-10 exponent k such that abs_val * 10^(-k) is in [10^16, 10^17)
    i32 bin_exp = (ieee_exp == 0) ? -1022 : (static_cast<i32>(ieee_exp) - 1023);
    u64 sig = (ieee_exp == 0) ? ieee_mant : ((1ULL << 52) | ieee_mant);
    const int lz = countl_zero(sig);
    bin_exp -= (lz - 11);

    // log10(2) ~= 78913 / 262144
    const i32 approx_log10 = ((bin_exp * 78913) >> 18);
    i32 exp10 = approx_log10 - 16;

    // Scale `abs_val` into a 17-digit decimal integer `dec17` using our high-precision Fp128 multiplier
    const Fp128 v_fp{sig << lz, 0ULL, (ieee_exp == 0 ? -1074 : static_cast<i32>(ieee_exp) - 1075) + (63 - lz)};
    const Fp128 scale = compute_pow10(-exp10);
    const Fp128 scaled = fp128_mul(v_fp, scale);

    u64 dec = 0;
    if (scaled.exp2 >= 0 && scaled.exp2 <= 63) {
        const int shift = 63 - scaled.exp2;
        dec = (shift == 0) ? scaled.hi : (scaled.hi >> shift);
        if (shift > 0 && ((scaled.hi >> (shift - 1)) & 1ULL) != 0) {
            ++dec;
        }
    }

    // Refine `dec` if needed so `mantissa_exp10_to_f64(dec, exp10)` equals `abs_val`
    const u64 target_bits = f64_to_bits(abs_val);
    if (f64_to_bits(mantissa_exp10_to_f64(dec, exp10, false, false)) != target_bits) {
        for (int delta = 1; delta <= 4; ++delta) {
            if (dec >= static_cast<u64>(delta) &&
                f64_to_bits(mantissa_exp10_to_f64(dec - delta, exp10, false, false)) == target_bits) {
                dec -= static_cast<u64>(delta);
                break;
            }
            if (f64_to_bits(mantissa_exp10_to_f64(dec + delta, exp10, false, false)) == target_bits) {
                dec += static_cast<u64>(delta);
                break;
            }
        }
    }

    // Trim digits from `dec` while preserving exact IEEE-754 round-trip to find the shortest representation!
    while (dec >= 10ULL) {
        const u64 q = dec / 10ULL;
        const u64 r = dec % 10ULL;
        const u64 rounded = (r >= 5ULL) ? (q + 1ULL) : q;
        if (f64_to_bits(mantissa_exp10_to_f64(rounded, exp10 + 1, false, false)) == target_bits) {
            dec = rounded;
            ++exp10;
            continue;
        }
        const u64 alt = (r >= 5ULL) ? q : (q + 1ULL);
        if (alt > 0 && f64_to_bits(mantissa_exp10_to_f64(alt, exp10 + 1, false, false)) == target_bits) {
            dec = alt;
            ++exp10;
            continue;
        }
        break;
    }

    const int num_digits = count_decimal_digits(dec);
    return emit_decimal_sci_or_fixed(buf, cap, negative, dec, num_digits, exp10);
}

isize format_f32(char* buf, isize cap, f32 value) noexcept {
    if (cap <= 0) return 0;

    const u32 bits = f32_to_bits(value);
    const bool negative = (bits >> 31) != 0;
    const u32 ieee_exp = (bits >> 23) & 0xFFu;
    const u32 ieee_mant = bits & ((1u << 23) - 1u);

    if (ieee_exp == 0xFFu) {
        const StringRef s = (ieee_mant != 0) ? StringRef("nan") : (negative ? StringRef("-inf") : StringRef("inf"));
        const isize n = min(s.size(), cap);
        mem_copy(buf, s.data(), static_cast<usize>(n));
        return n;
    }

    if (ieee_exp == 0 && ieee_mant == 0) {
        const StringRef s = negative ? StringRef("-0") : StringRef("0");
        const isize n = min(s.size(), cap);
        mem_copy(buf, s.data(), static_cast<usize>(n));
        return n;
    }

    const f32 abs_val = negative ? -value : value;
    const u32 target_bits = f32_to_bits(abs_val);

    i32 bin_exp = (ieee_exp == 0) ? -126 : (static_cast<i32>(ieee_exp) - 127);
    u64 sig = (ieee_exp == 0) ? ieee_mant : ((1ULL << 23) | ieee_mant);
    const int lz = countl_zero(sig);
    bin_exp -= (lz - 40);

    const i32 approx_log10 = ((bin_exp * 78913) >> 18);
    i32 exp10 = approx_log10 - 8;

    const Fp128 v_fp{sig << lz, 0ULL, (ieee_exp == 0 ? -149 : static_cast<i32>(ieee_exp) - 150) + (63 - lz)};
    const Fp128 scale = compute_pow10(-exp10);
    const Fp128 scaled = fp128_mul(v_fp, scale);

    u64 dec = 0;
    if (scaled.exp2 >= 0 && scaled.exp2 <= 63) {
        const int shift = 63 - scaled.exp2;
        dec = (shift == 0) ? scaled.hi : (scaled.hi >> shift);
        if (shift > 0 && ((scaled.hi >> (shift - 1)) & 1ULL) != 0) {
            ++dec;
        }
    }

    auto round_trips_f32 = [&](u64 m, i32 e10) noexcept -> bool {
        const f32 candidate = static_cast<f32>(mantissa_exp10_to_f64(m, e10, false, false));
        return f32_to_bits(candidate) == target_bits;
    };

    if (!round_trips_f32(dec, exp10)) {
        for (int delta = 1; delta <= 4; ++delta) {
            if (dec >= static_cast<u64>(delta) && round_trips_f32(dec - delta, exp10)) {
                dec -= static_cast<u64>(delta);
                break;
            }
            if (round_trips_f32(dec + delta, exp10)) {
                dec += static_cast<u64>(delta);
                break;
            }
        }
    }

    while (dec >= 10ULL) {
        const u64 q = dec / 10ULL;
        const u64 r = dec % 10ULL;
        const u64 rounded = (r >= 5ULL) ? (q + 1ULL) : q;
        if (round_trips_f32(rounded, exp10 + 1)) {
            dec = rounded;
            ++exp10;
            continue;
        }
        const u64 alt = (r >= 5ULL) ? q : (q + 1ULL);
        if (alt > 0 && round_trips_f32(alt, exp10 + 1)) {
            dec = alt;
            ++exp10;
            continue;
        }
        break;
    }

    const int num_digits = count_decimal_digits(dec);
    return emit_decimal_sci_or_fixed(buf, cap, negative, dec, num_digits, exp10);
}

isize format_f64_fixed(char* buf, isize cap, f64 value, int precision) noexcept {
    if (cap <= 0) return 0;
    if (precision < 0) precision = 6;
    if (precision > 18) precision = 18;

    const u64 bits = f64_to_bits(value);
    const bool negative = (bits >> 63) != 0;
    const u64 ieee_exp = (bits >> 52) & 0x7FFULL;
    const u64 ieee_mant = bits & ((1ULL << 52) - 1ULL);

    if (ieee_exp == 0x7FFULL) {
        const StringRef s = (ieee_mant != 0) ? StringRef("nan") : (negative ? StringRef("-inf") : StringRef("inf"));
        const isize n = min(s.size(), cap);
        mem_copy(buf, s.data(), static_cast<usize>(n));
        return n;
    }

    f64 abs_val = negative ? -value : value;
    if (abs_val > 1e18) {
        return format_f64(buf, cap, value);
    }

    u64 scale = 1ULL;
    for (int i = 0; i < precision; ++i) scale *= 10ULL;

    const f64 scaled = abs_val * static_cast<f64>(scale);
    u64 rounded = static_cast<u64>(scaled + 0.5);
    const u64 int_part = rounded / scale;
    const u64 frac_part = rounded % scale;

    char tmp[64];
    isize pos = 0;
    if (negative) tmp[pos++] = '-';
    pos += format_u64(tmp + pos, static_cast<isize>(sizeof(tmp)) - pos, int_part, 10, false);

    if (precision > 0) {
        tmp[pos++] = '.';
        u64 f = frac_part;
        for (int i = precision - 1; i >= 0; --i) {
            tmp[pos + i] = static_cast<char>('0' + (f % 10ULL));
            f /= 10ULL;
        }
        pos += precision;
    }

    const isize copy_len = min(pos, cap);
    mem_copy(buf, tmp, static_cast<usize>(copy_len));
    return copy_len;
}

} // namespace keel
