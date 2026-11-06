#include "keel/format.h"
#include "keel/io.h"

namespace keel {

namespace {

void format_arg_into(String& out, char spec, int precision, const FormatArg& arg) {
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
        out.append(StringRef(num_buf, n));
        break;
    }
    case 'u': {
        const u64 v = (arg.kind == FormatArg::Kind::Signed) ? static_cast<u64>(arg.i_val) : arg.u_val;
        const isize n = format_u64(num_buf, sizeof(num_buf), v, 10, false);
        out.append(StringRef(num_buf, n));
        break;
    }
    case 'x':
    case 'X': {
        const u64 v = (arg.kind == FormatArg::Kind::Signed) ? static_cast<u64>(arg.i_val) : arg.u_val;
        const isize n = format_u64(num_buf, sizeof(num_buf), v, 16, spec == 'X');
        out.append(StringRef(num_buf, n));
        break;
    }
    case 'p': {
        out.append("0x");
        const u64 v = (arg.kind == FormatArg::Kind::Pointer)
                          ? static_cast<u64>(reinterpret_cast<usize>(arg.ptr_val))
                          : arg.u_val;
        const isize n = format_u64(num_buf, sizeof(num_buf), v, 16, false);
        out.append(StringRef(num_buf, n));
        break;
    }
    case 'f':
    case 'F': {
        const f64 v = (arg.kind == FormatArg::Kind::Signed)
                          ? static_cast<f64>(arg.i_val)
                          : ((arg.kind == FormatArg::Kind::Unsigned) ? static_cast<f64>(arg.u_val) : arg.f_val);
        const isize n = format_f64_fixed(num_buf, sizeof(num_buf), v, precision >= 0 ? precision : 6);
        out.append(StringRef(num_buf, n));
        break;
    }
    case 'g':
    case 'G':
    case 'e':
    case 'E':
    default: {
        if (arg.kind == FormatArg::Kind::Float) {
            const isize n = format_f32(num_buf, sizeof(num_buf), static_cast<f32>(arg.f_val));
            out.append(StringRef(num_buf, n));
        } else if (arg.kind == FormatArg::Kind::Double) {
            const isize n = (precision >= 0) ? format_f64_fixed(num_buf, sizeof(num_buf), arg.f_val, precision)
                                             : format_f64(num_buf, sizeof(num_buf), arg.f_val);
            out.append(StringRef(num_buf, n));
        } else if (arg.kind == FormatArg::Kind::Signed) {
            const isize n = format_i64(num_buf, sizeof(num_buf), arg.i_val, 10, false);
            out.append(StringRef(num_buf, n));
        } else if (arg.kind == FormatArg::Kind::Unsigned) {
            const isize n = format_u64(num_buf, sizeof(num_buf), arg.u_val, 10, false);
            out.append(StringRef(num_buf, n));
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
            out.append(StringRef(num_buf, n));
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
        if (fmt[i] != '%') {
            out.push_back(fmt[i]);
            continue;
        }
        if (i + 1 < len && fmt[i + 1] == '%') {
            out.push_back('%');
            ++i;
            continue;
        }

        ++i;
        // Skip optional flags/width (`-`, `+`, `0`, digits)
        while (i < len && (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' ||
                           (fmt[i] >= '0' && fmt[i] <= '9'))) {
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
                format_arg_into(out, spec, precision, args[arg_idx++]);
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
