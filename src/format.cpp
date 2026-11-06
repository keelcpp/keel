#include "keel/format.h"
#include "keel/io.h"

namespace keel {

namespace {

void append_padded_num(String& out, const char* num_buf, isize n, int width, bool zero_pad, bool plus_sign) {
    if (n <= 0) return;
    const bool is_neg = (num_buf[0] == '-');
    const bool has_sign = is_neg || plus_sign;
    const char sign_ch = is_neg ? '-' : '+';
    const char* digits = is_neg ? (num_buf + 1) : num_buf;
    const isize digit_len = is_neg ? (n - 1) : n;
    const isize total_len = digit_len + (has_sign ? 1 : 0);
    const isize pad_count = (width > total_len) ? (static_cast<isize>(width) - total_len) : 0;

    if (!zero_pad) {
        for (isize p = 0; p < pad_count; ++p) out.push_back(' ');
    }
    if (has_sign) {
        out.push_back(sign_ch);
    }
    if (zero_pad) {
        for (isize p = 0; p < pad_count; ++p) out.push_back('0');
    }
    out.append(StringRef(digits, digit_len));
}

void format_arg_into(
    String& out,
    char spec,
    int width,
    bool zero_pad,
    bool plus_sign,
    int precision,
    const FormatArg& arg
) {
    char num_buf[64];
    switch (spec) {
    case 'S':
    case 's': {
        if (arg.kind == FormatArg::Kind::StringRef) {
            out.append(StringRef(arg.cstr_val, arg.str_len));
        } else if (arg.kind == FormatArg::Kind::CString) {
            out.append(StringRef(arg.cstr_val));
        } else if (arg.kind == FormatArg::Kind::Bool) {
            out.append(arg.u_val ? "true" : "false");
        }
        break;
    }
    case 'c': {
        if (arg.kind == FormatArg::Kind::Char || arg.kind == FormatArg::Kind::Signed) {
            out.push_back(static_cast<char>(arg.i_val));
        } else if (arg.kind == FormatArg::Kind::Unsigned) {
            out.push_back(static_cast<char>(arg.u_val));
        }
        break;
    }
    case 'd':
    case 'i': {
        const i64 v = (arg.kind == FormatArg::Kind::Unsigned) ? static_cast<i64>(arg.u_val) : arg.i_val;
        const isize n = format_i64(num_buf, sizeof(num_buf), v, 10, false);
        append_padded_num(out, num_buf, n, width, zero_pad, plus_sign);
        break;
    }
    case 'u': {
        const u64 v = (arg.kind == FormatArg::Kind::Signed) ? static_cast<u64>(arg.i_val) : arg.u_val;
        const isize n = format_u64(num_buf, sizeof(num_buf), v, 10, false);
        append_padded_num(out, num_buf, n, width, zero_pad, plus_sign);
        break;
    }
    case 'x':
    case 'X': {
        const u64 v = (arg.kind == FormatArg::Kind::Signed) ? static_cast<u64>(arg.i_val) : arg.u_val;
        const isize n = format_u64(num_buf, sizeof(num_buf), v, 16, spec == 'X');
        append_padded_num(out, num_buf, n, width, zero_pad, false);
        break;
    }
    case 'p': {
        out.append("0x");
        const u64 v = (arg.kind == FormatArg::Kind::Pointer)
                          ? static_cast<u64>(reinterpret_cast<usize>(arg.ptr_val))
                          : arg.u_val;
        const isize n = format_u64(num_buf, sizeof(num_buf), v, 16, false);
        append_padded_num(out, num_buf, n, width, zero_pad, false);
        break;
    }
    case 'f':
    case 'F': {
        const f64 v = (arg.kind == FormatArg::Kind::Signed)
                          ? static_cast<f64>(arg.i_val)
                          : ((arg.kind == FormatArg::Kind::Unsigned) ? static_cast<f64>(arg.u_val) : arg.f_val);
        const isize n = format_f64_fixed(num_buf, sizeof(num_buf), v, precision >= 0 ? precision : 6);
        append_padded_num(out, num_buf, n, width, zero_pad, plus_sign);
        break;
    }
    case 'g':
    case 'G':
    case 'e':
    case 'E':
    default: {
        if (arg.kind == FormatArg::Kind::Float) {
            const isize n = (precision >= 0) ? format_f64_fixed(num_buf, sizeof(num_buf), arg.f_val, precision)
                                             : format_f32(num_buf, sizeof(num_buf), static_cast<f32>(arg.f_val));
            append_padded_num(out, num_buf, n, width, zero_pad, plus_sign);
        } else if (arg.kind == FormatArg::Kind::Double) {
            const isize n = (precision >= 0) ? format_f64_fixed(num_buf, sizeof(num_buf), arg.f_val, precision)
                                             : format_f64(num_buf, sizeof(num_buf), arg.f_val);
            append_padded_num(out, num_buf, n, width, zero_pad, plus_sign);
        } else if (arg.kind == FormatArg::Kind::Signed) {
            const isize n = format_i64(num_buf, sizeof(num_buf), arg.i_val, 10, false);
            append_padded_num(out, num_buf, n, width, zero_pad, plus_sign);
        } else if (arg.kind == FormatArg::Kind::Unsigned) {
            const isize n = format_u64(num_buf, sizeof(num_buf), arg.u_val, 10, false);
            append_padded_num(out, num_buf, n, width, zero_pad, plus_sign);
        } else if (arg.kind == FormatArg::Kind::Bool) {
            out.append(arg.u_val ? "true" : "false");
        } else if (arg.kind == FormatArg::Kind::Char) {
            out.push_back(static_cast<char>(arg.i_val));
        } else if (arg.kind == FormatArg::Kind::StringRef) {
            out.append(StringRef(arg.cstr_val, arg.str_len));
        } else if (arg.kind == FormatArg::Kind::CString) {
            out.append(StringRef(arg.cstr_val));
        } else if (arg.kind == FormatArg::Kind::Pointer) {
            out.append("0x");
            const isize n =
                format_u64(num_buf, sizeof(num_buf), static_cast<u64>(reinterpret_cast<usize>(arg.ptr_val)), 16, false);
            append_padded_num(out, num_buf, n, width, zero_pad, false);
        }
        break;
    }
    }
}

} // namespace

void vformat_into(String& out, StringRef fmt, ArrayRef<FormatArg> args) {
    isize arg_idx = 0;
    const isize len = fmt.size();
    for (isize i = 0; i < len; ++i) {
        const char ch = fmt[i];
        if (ch == '}') {
            if (i + 1 < len && fmt[i + 1] == '}') {
                out.push_back('}');
                ++i;
            } else {
                out.push_back('}');
            }
            continue;
        }

        if (ch == '{') {
            if (i + 1 < len && fmt[i + 1] == '{') {
                out.push_back('{');
                ++i;
                continue;
            }
            isize close_pos = i + 1;
            while (close_pos < len && fmt[close_pos] != '}') {
                ++close_pos;
            }
            if (close_pos < len) {
                char spec = '\0';
                int width = 0;
                bool zero_pad = false;
                bool plus_sign = false;
                int precision = -1;

                isize p = i + 1;
                while (p < close_pos && fmt[p] != ':') {
                    ++p;
                }
                if (p < close_pos && fmt[p] == ':') {
                    ++p;
                    while (p < close_pos && (fmt[p] == '-' || fmt[p] == '+' || fmt[p] == ' ' || fmt[p] == '#')) {
                        if (fmt[p] == '+') plus_sign = true;
                        ++p;
                    }
                    if (p < close_pos && fmt[p] == '0') {
                        zero_pad = true;
                        ++p;
                    }
                    while (p < close_pos && fmt[p] >= '0' && fmt[p] <= '9') {
                        width = width * 10 + (fmt[p] - '0');
                        ++p;
                    }
                    if (p < close_pos && fmt[p] == '.') {
                        ++p;
                        precision = 0;
                        while (p < close_pos && fmt[p] >= '0' && fmt[p] <= '9') {
                            precision = precision * 10 + (fmt[p] - '0');
                            ++p;
                        }
                    }
                    if (p < close_pos) {
                        spec = fmt[p];
                    }
                }
                if (arg_idx < args.size()) {
                    format_arg_into(out, spec, width, zero_pad, plus_sign, precision, args[arg_idx++]);
                }
                i = close_pos;
                continue;
            }
        }

        if (ch != '%') {
            out.push_back(ch);
            continue;
        }
        if (i + 1 < len && fmt[i + 1] == '%') {
            out.push_back('%');
            ++i;
            continue;
        }

        ++i;
        bool zero_pad = false;
        bool plus_sign = false;
        while (i < len && (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0')) {
            if (fmt[i] == '0') zero_pad = true;
            if (fmt[i] == '+') plus_sign = true;
            ++i;
        }

        int width = 0;
        while (i < len && fmt[i] >= '0' && fmt[i] <= '9') {
            width = width * 10 + (fmt[i] - '0');
            ++i;
        }

        int precision = -1;
        if (i < len && fmt[i] == '.') {
            ++i;
            precision = 0;
            while (i < len && fmt[i] >= '0' && fmt[i] <= '9') {
                precision = precision * 10 + (fmt[i] - '0');
                ++i;
            }
        }

        // Skip optional length modifiers (`l`, `ll`, `z`, `h`, `hh`)
        while (i < len && (fmt[i] == 'l' || fmt[i] == 'z' || fmt[i] == 'h')) {
            ++i;
        }

        if (i < len) {
            const char spec = fmt[i];
            if (arg_idx < args.size()) {
                format_arg_into(out, spec, width, zero_pad, plus_sign, precision, args[arg_idx++]);
            }
        }
    }
}

isize vformat_buf(char* buf, isize cap, StringRef fmt, ArrayRef<FormatArg> args) noexcept {
    if (!buf || cap <= 0) return 0;
    ArenaTemp scratch = get_scratch();
    String tmp(scratch.allocator());
    vformat_into(tmp, fmt, args);
    const isize copy_len = min(tmp.size(), cap - 1);
    mem_copy(buf, tmp.data(), static_cast<usize>(copy_len));
    buf[copy_len] = '\0';
    return copy_len;
}

StringRef vformat_arena(Arena& arena, StringRef fmt, ArrayRef<FormatArg> args) {
    String out(arena.allocator());
    vformat_into(out, fmt, args);
    const isize len = out.size();
    char* copy = static_cast<char*>(arena.alloc(len + 1, 1));
    mem_copy(copy, out.c_str(), static_cast<usize>(len + 1));
    return StringRef(copy, len);
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
