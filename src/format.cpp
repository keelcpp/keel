#include "keel/format.h"
#include "keel/io.h"

namespace keel {

namespace {

void append_aligned_text(String& out, StringRef text, int width, char align, char fill) {
    const usize total_len = text.size();
    const usize pad_count =
        (width > 0 && static_cast<usize>(width) > total_len) ? (static_cast<usize>(width) - total_len) : 0;
    if (pad_count == 0) {
        out.append(text);
        return;
    }
    const char eff_align = (align == '\0') ? '<' : align;
    const char eff_fill = (fill == '\0') ? ' ' : fill;
    if (eff_align == '>') {
        for (usize p = 0; p < pad_count; ++p) out.push_back(eff_fill);
        out.append(text);
    } else if (eff_align == '^') {
        const usize left_pad = pad_count / 2;
        const usize right_pad = pad_count - left_pad;
        for (usize p = 0; p < left_pad; ++p) out.push_back(eff_fill);
        out.append(text);
        for (usize p = 0; p < right_pad; ++p) out.push_back(eff_fill);
    } else {
        out.append(text);
        for (usize p = 0; p < pad_count; ++p) out.push_back(eff_fill);
    }
}

void append_padded_num(
    String& out,
    const char* num_buf,
    usize n,
    int width,
    bool zero_pad,
    bool plus_sign,
    char align = '\0',
    char fill = ' '
) {
    if (n == 0) return;
    const bool is_neg = (num_buf[0] == '-');
    const bool has_sign = is_neg || plus_sign;
    const char sign_ch = is_neg ? '-' : '+';
    const char* digits = is_neg ? (num_buf + 1) : num_buf;
    const usize digit_len = is_neg ? (n - 1) : n;
    const usize total_len = digit_len + (has_sign ? 1u : 0u);
    const usize pad_count =
        (width > 0 && static_cast<usize>(width) > total_len) ? (static_cast<usize>(width) - total_len) : 0;

    if (zero_pad && align == '\0') {
        if (has_sign) out.push_back(sign_ch);
        for (usize p = 0; p < pad_count; ++p) out.push_back('0');
        out.append(StringRef(digits, digit_len));
        return;
    }

    const char eff_align = (align == '\0') ? '>' : align;
    const char eff_fill = (fill == '\0') ? ' ' : fill;
    if (eff_align == '<') {
        if (has_sign) out.push_back(sign_ch);
        out.append(StringRef(digits, digit_len));
        for (usize p = 0; p < pad_count; ++p) out.push_back(eff_fill);
    } else if (eff_align == '^') {
        const usize left_pad = pad_count / 2;
        const usize right_pad = pad_count - left_pad;
        for (usize p = 0; p < left_pad; ++p) out.push_back(eff_fill);
        if (has_sign) out.push_back(sign_ch);
        out.append(StringRef(digits, digit_len));
        for (usize p = 0; p < right_pad; ++p) out.push_back(eff_fill);
    } else {
        for (usize p = 0; p < pad_count; ++p) out.push_back(eff_fill);
        if (has_sign) out.push_back(sign_ch);
        out.append(StringRef(digits, digit_len));
    }
}

void format_arg_into(
    String& out,
    char spec,
    int width,
    bool zero_pad,
    bool plus_sign,
    int precision,
    const FormatArg& arg,
    char align = '\0',
    char fill = ' '
) {
    char num_buf[64];
    switch (spec) {
    case 'S':
    case 's': {
        StringRef sv{};
        if (arg.kind == FormatArg::Kind::StringRef) {
            sv = StringRef(arg.cstr_val, arg.str_len);
        } else if (arg.kind == FormatArg::Kind::CString) {
            if (precision >= 0) {
                usize len = 0;
                const usize max_len = static_cast<usize>(precision);
                while (len < max_len && arg.cstr_val[len] != '\0') ++len;
                sv = StringRef(arg.cstr_val, len);
            } else {
                sv = StringRef(arg.cstr_val);
            }
        } else if (arg.kind == FormatArg::Kind::Bool) {
            sv = arg.u_val ? StringRef("true") : StringRef("false");
        }
        if (precision >= 0 && static_cast<usize>(precision) < sv.size()) {
            sv = sv.substr(0, static_cast<usize>(precision));
        }
        append_aligned_text(out, sv, width, align, fill);
        break;
    }
    case 'c': {
        char ch = '\0';
        bool has_ch = false;
        if (arg.kind == FormatArg::Kind::Char || arg.kind == FormatArg::Kind::Signed) {
            ch = static_cast<char>(arg.i_val);
            has_ch = true;
        } else if (arg.kind == FormatArg::Kind::Unsigned) {
            ch = static_cast<char>(arg.u_val);
            has_ch = true;
        }
        if (has_ch) {
            append_aligned_text(out, StringRef(&ch, 1), width, align, fill);
        }
        break;
    }
    case 'd':
    case 'i': {
        const i64 v = (arg.kind == FormatArg::Kind::Unsigned) ? static_cast<i64>(arg.u_val) : arg.i_val;
        const usize n = format_i64(num_buf, sizeof(num_buf), v, 10, false);
        append_padded_num(out, num_buf, n, width, zero_pad, plus_sign, align, fill);
        break;
    }
    case 'u': {
        const u64 v = (arg.kind == FormatArg::Kind::Signed) ? static_cast<u64>(arg.i_val) : arg.u_val;
        const usize n = format_u64(num_buf, sizeof(num_buf), v, 10, false);
        append_padded_num(out, num_buf, n, width, zero_pad, plus_sign, align, fill);
        break;
    }
    case 'x':
    case 'X': {
        const u64 v = (arg.kind == FormatArg::Kind::Signed) ? static_cast<u64>(arg.i_val) : arg.u_val;
        const usize n = format_u64(num_buf, sizeof(num_buf), v, 16, spec == 'X');
        append_padded_num(out, num_buf, n, width, zero_pad, false, align, fill);
        break;
    }
    case 'p': {
        out.append("0x");
        const u64 v = (arg.kind == FormatArg::Kind::Pointer)
                          ? static_cast<u64>(reinterpret_cast<usize>(arg.ptr_val))
                          : arg.u_val;
        const usize n = format_u64(num_buf, sizeof(num_buf), v, 16, false);
        append_padded_num(out, num_buf, n, width, zero_pad, false, align, fill);
        break;
    }
    case 'f':
    case 'F': {
        const f64 v = (arg.kind == FormatArg::Kind::Signed)
                          ? static_cast<f64>(arg.i_val)
                          : ((arg.kind == FormatArg::Kind::Unsigned) ? static_cast<f64>(arg.u_val) : arg.f_val);
        const usize n = format_f64_fixed(num_buf, sizeof(num_buf), v, precision >= 0 ? precision : 6);
        append_padded_num(out, num_buf, n, width, zero_pad, plus_sign, align, fill);
        break;
    }
    case 'g':
    case 'G': {
        const f64 v = (arg.kind == FormatArg::Kind::Signed)
                          ? static_cast<f64>(arg.i_val)
                          : ((arg.kind == FormatArg::Kind::Unsigned) ? static_cast<f64>(arg.u_val) : arg.f_val);
        const usize n = format_f64_general(num_buf, sizeof(num_buf), v, precision, spec == 'G');
        append_padded_num(out, num_buf, n, width, zero_pad, plus_sign, align, fill);
        break;
    }
    case 'e':
    case 'E': {
        const f64 v = (arg.kind == FormatArg::Kind::Signed)
                          ? static_cast<f64>(arg.i_val)
                          : ((arg.kind == FormatArg::Kind::Unsigned) ? static_cast<f64>(arg.u_val) : arg.f_val);
        const usize n = format_f64_scientific(num_buf, sizeof(num_buf), v, precision, spec == 'E');
        append_padded_num(out, num_buf, n, width, zero_pad, plus_sign, align, fill);
        break;
    }
    default: {
        if (arg.kind == FormatArg::Kind::Float) {
            const usize n = (precision >= 0) ? format_f64_fixed(num_buf, sizeof(num_buf), arg.f_val, precision)
                                             : format_f32(num_buf, sizeof(num_buf), static_cast<f32>(arg.f_val));
            append_padded_num(out, num_buf, n, width, zero_pad, plus_sign, align, fill);
        } else if (arg.kind == FormatArg::Kind::Double) {
            const usize n = (precision >= 0) ? format_f64_fixed(num_buf, sizeof(num_buf), arg.f_val, precision)
                                             : format_f64(num_buf, sizeof(num_buf), arg.f_val);
            append_padded_num(out, num_buf, n, width, zero_pad, plus_sign, align, fill);
        } else if (arg.kind == FormatArg::Kind::Signed) {
            const usize n = format_i64(num_buf, sizeof(num_buf), arg.i_val, 10, false);
            append_padded_num(out, num_buf, n, width, zero_pad, plus_sign, align, fill);
        } else if (arg.kind == FormatArg::Kind::Unsigned) {
            const usize n = format_u64(num_buf, sizeof(num_buf), arg.u_val, 10, false);
            append_padded_num(out, num_buf, n, width, zero_pad, plus_sign, align, fill);
        } else if (arg.kind == FormatArg::Kind::Bool) {
            append_aligned_text(out, arg.u_val ? StringRef("true") : StringRef("false"), width, align, fill);
        } else if (arg.kind == FormatArg::Kind::Char) {
            const char ch = static_cast<char>(arg.i_val);
            append_aligned_text(out, StringRef(&ch, 1), width, align, fill);
        } else if (arg.kind == FormatArg::Kind::StringRef) {
            StringRef sv(arg.cstr_val, arg.str_len);
            if (precision >= 0 && static_cast<usize>(precision) < sv.size()) {
                sv = sv.substr(0, static_cast<usize>(precision));
            }
            append_aligned_text(out, sv, width, align, fill);
        } else if (arg.kind == FormatArg::Kind::CString) {
            StringRef sv(arg.cstr_val);
            if (precision >= 0 && static_cast<usize>(precision) < sv.size()) {
                sv = sv.substr(0, static_cast<usize>(precision));
            }
            append_aligned_text(out, sv, width, align, fill);
        } else if (arg.kind == FormatArg::Kind::Pointer) {
            out.append("0x");
            const usize n =
                format_u64(num_buf, sizeof(num_buf), static_cast<u64>(reinterpret_cast<usize>(arg.ptr_val)), 16, false);
            append_padded_num(out, num_buf, n, width, zero_pad, false, align, fill);
        }
        break;
    }
    }
}

bool is_printf_spec(char c) noexcept {
    switch (c) {
    case 'd':
    case 'i':
    case 'u':
    case 'x':
    case 'X':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'e':
    case 'E':
    case 's':
    case 'S':
    case 'c':
    case 'p':
        return true;
    default:
        return false;
    }
}

struct BraceSpec {
    bool has_explicit_index = false;
    usize explicit_index = 0;
    char spec = '\0';
    char align = '\0';
    char fill = ' ';
    int width = 0;
    bool zero_pad = false;
    bool plus_sign = false;
    int precision = -1;
};

bool parse_brace_placeholder(StringRef fmt, usize open_pos, usize& out_close_pos, BraceSpec& out_spec) noexcept {
    const usize len = fmt.size();
    usize close_pos = open_pos + 1;
    while (close_pos < len && fmt[close_pos] != '}' && fmt[close_pos] != '{') {
        ++close_pos;
    }
    if (close_pos >= len || fmt[close_pos] != '}') return false;

    BraceSpec bs{};
    usize p = open_pos + 1;
    if (p < close_pos && fmt[p] >= '0' && fmt[p] <= '9') {
        bs.has_explicit_index = true;
        while (p < close_pos && fmt[p] >= '0' && fmt[p] <= '9') {
            bs.explicit_index = bs.explicit_index * 10 + static_cast<usize>(fmt[p] - '0');
            ++p;
        }
    }

    if (p < close_pos) {
        if (fmt[p] != ':') return false;
        ++p;
        // Optional [fill]align (`<`, `>`, `^`)
        if (p + 1 < close_pos && (fmt[p + 1] == '<' || fmt[p + 1] == '>' || fmt[p + 1] == '^')) {
            bs.fill = fmt[p];
            bs.align = fmt[p + 1];
            p += 2;
        } else if (p < close_pos && (fmt[p] == '<' || fmt[p] == '>' || fmt[p] == '^')) {
            bs.align = fmt[p++];
        }
        // Optional sign / flags
        while (p < close_pos && (fmt[p] == '-' || fmt[p] == '+' || fmt[p] == ' ' || fmt[p] == '#')) {
            if (fmt[p] == '+') bs.plus_sign = true;
            if (fmt[p] == '-' && bs.align == '\0') bs.align = '<';
            ++p;
        }
        if (p < close_pos && fmt[p] == '0') {
            bs.zero_pad = true;
            ++p;
        }
        while (p < close_pos && fmt[p] >= '0' && fmt[p] <= '9') {
            bs.width = bs.width * 10 + (fmt[p] - '0');
            ++p;
        }
        if (p < close_pos && fmt[p] == '.') {
            ++p;
            bs.precision = 0;
            while (p < close_pos && fmt[p] >= '0' && fmt[p] <= '9') {
                bs.precision = bs.precision * 10 + (fmt[p] - '0');
                ++p;
            }
        }
        if (p < close_pos) {
            if (!is_printf_spec(fmt[p])) return false;
            bs.spec = fmt[p++];
        }
        if (p != close_pos) return false;
    }

    out_close_pos = close_pos;
    out_spec = bs;
    return true;
}

bool has_brace_placeholder(StringRef fmt) noexcept {
    const usize len = fmt.size();
    for (usize i = 0; i < len; ++i) {
        if (fmt[i] == '{') {
            if (i + 1 < len && fmt[i + 1] == '{') {
                ++i;
                continue;
            }
            usize close_pos = 0;
            BraceSpec bs{};
            if (parse_brace_placeholder(fmt, i, close_pos, bs)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

void vformat_into(String& out, StringRef fmt, ArrayRef<FormatArg> args) {
    usize arg_idx = 0;
    const usize len = fmt.size();
    const bool brace_mode = has_brace_placeholder(fmt);
    for (usize i = 0; i < len; ++i) {
        const char ch = fmt[i];
        if (ch == '}') {
            if (brace_mode && i + 1 < len && fmt[i + 1] == '}') {
                out.push_back('}');
                ++i;
            } else {
                out.push_back('}');
            }
            continue;
        }

        if (ch == '{') {
            if (brace_mode && i + 1 < len && fmt[i + 1] == '{') {
                out.push_back('{');
                ++i;
                continue;
            }
            usize close_pos = 0;
            BraceSpec bs{};
            if (parse_brace_placeholder(fmt, i, close_pos, bs)) {
                const usize target_idx = bs.has_explicit_index ? bs.explicit_index : arg_idx++;
                if (target_idx < args.size()) {
                    format_arg_into(
                        out, bs.spec, bs.width, bs.zero_pad, bs.plus_sign, bs.precision, args[target_idx], bs.align, bs.fill
                    );
                }
                i = close_pos;
                continue;
            }
        }

        if (ch != '%' || brace_mode) {
            out.push_back(ch);
            continue;
        }
        if (i + 1 < len && fmt[i + 1] == '%') {
            out.push_back('%');
            ++i;
            continue;
        }

        usize p = i + 1;
        bool zero_pad = false;
        bool plus_sign = false;
        bool left_align = false;
        while (p < len && (fmt[p] == '-' || fmt[p] == '+' || fmt[p] == ' ' || fmt[p] == '#' || fmt[p] == '0')) {
            if (fmt[p] == '0') zero_pad = true;
            if (fmt[p] == '+') plus_sign = true;
            if (fmt[p] == '-') left_align = true;
            ++p;
        }

        int width = 0;
        bool has_dyn_width = false;
        if (p < len && fmt[p] == '*') {
            has_dyn_width = true;
            ++p;
        } else {
            while (p < len && fmt[p] >= '0' && fmt[p] <= '9') {
                width = width * 10 + (fmt[p] - '0');
                ++p;
            }
        }

        int precision = -1;
        bool has_dyn_prec = false;
        if (p < len && fmt[p] == '.') {
            ++p;
            if (p < len && fmt[p] == '*') {
                has_dyn_prec = true;
                ++p;
            } else {
                precision = 0;
                while (p < len && fmt[p] >= '0' && fmt[p] <= '9') {
                    precision = precision * 10 + (fmt[p] - '0');
                    ++p;
                }
            }
        }

        // Skip optional length modifiers (`l`, `ll`, `z`, `h`, `hh`)
        while (p < len && (fmt[p] == 'l' || fmt[p] == 'z' || fmt[p] == 'h')) {
            ++p;
        }

        if (p < len && is_printf_spec(fmt[p])) {
            const char spec = fmt[p];
            if (has_dyn_width && arg_idx < args.size()) {
                const FormatArg& wa = args[arg_idx++];
                const i64 wv = (wa.kind == FormatArg::Kind::Unsigned) ? static_cast<i64>(wa.u_val) : wa.i_val;
                if (wv < 0) {
                    left_align = true;
                    width = static_cast<int>(-wv);
                } else {
                    width = static_cast<int>(wv);
                }
            }
            if (has_dyn_prec && arg_idx < args.size()) {
                const FormatArg& pa = args[arg_idx++];
                const i64 pv = (pa.kind == FormatArg::Kind::Unsigned) ? static_cast<i64>(pa.u_val) : pa.i_val;
                precision = (pv >= 0) ? static_cast<int>(pv) : -1;
            }
            if (arg_idx < args.size()) {
                const char align = left_align ? '<' : ((spec == 's' || spec == 'S' || spec == 'c') ? '>' : '\0');
                format_arg_into(out, spec, width, zero_pad, plus_sign, precision, args[arg_idx++], align, ' ');
            }
            i = p;
        } else {
            out.push_back('%');
        }
    }
}

usize vformat_buf(char* buf, usize cap, StringRef fmt, ArrayRef<FormatArg> args) noexcept {
    if (!buf || cap == 0) return 0;
    ArenaTemp scratch = get_scratch();
    String tmp(scratch.allocator());
    vformat_into(tmp, fmt, args);
    const usize copy_len = detail::clamp_utf8_truncation(tmp.data(), tmp.size(), cap - 1);
    if (copy_len > 0) {
        mem_copy(buf, tmp.data(), copy_len);
    }
    buf[copy_len] = '\0';
    return copy_len;
}

StringRef vformat_arena(Arena& arena, StringRef fmt, ArrayRef<FormatArg> args) {
    Arena* conflict = &arena;
    ArenaTemp scratch = get_scratch(&conflict, 1);
    String out(scratch.allocator());
    vformat_into(out, fmt, args);
    return arena.alloc_string(out.as_ref());
}

void vprint(bool to_stderr, bool newline, StringRef fmt, ArrayRef<FormatArg> args) noexcept {
    ArenaTemp scratch = get_scratch();
    String out(scratch.allocator());
    vformat_into(out, fmt, args);
    if (newline) {
        out.push_back('\n');
    }
    if (to_stderr) {
        stderr_write(out.as_ref());
    } else {
        stdout_write(out.as_ref());
    }
}

} // namespace keel
