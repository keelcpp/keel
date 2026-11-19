#include "keel/process.h"
#include "keel/env.h"
#include "keel/fs.h"
#include "keel/small_array.h"
#include "keel/utf8.h"

#if defined(__EMSCRIPTEN__)

namespace keel {

Result<ProcessOutput> process_run(ArrayRef<StringRef> /*argv*/, const ProcessOptions& /*opts*/, Allocator* /*alloc*/) {
    return Error{ErrorCode::NotSupported, 0};
}

Result<ProcessOutput> process_run_shell(StringRef /*command*/, const ProcessOptions& /*opts*/, Allocator* /*alloc*/) {
    return Error{ErrorCode::NotSupported, 0};
}

Optional<String> process_which(StringRef /*program*/, Allocator* /*alloc*/) { return kNullOpt; }

} // namespace keel

#elif defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace keel {

namespace detail {
Error win32_error_to_keel(DWORD err) noexcept;
bool utf8_path_to_win32_wide(StringRef utf8_path, SmallArray<wchar_t, 260>& out);

static bool proc_utf8_to_wide(StringRef utf8, Array<wchar_t>& out) {
    out.clear();
    if (utf8.empty()) {
        out.push_back(L'\0');
        return true;
    }
    if (!utf8_validate(utf8)) return false;
    const int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) return false;
    out.resize(static_cast<usize>(wlen) + 1);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                        out.data(), wlen);
    out[static_cast<usize>(wlen)] = L'\0';
    return true;
}

// Windows CreateProcessW argument quoting (Microsoft CommandLineToArgvW rules).
static void append_win32_arg(String& cmdline, StringRef arg) {
    bool needs_quotes = arg.empty();
    for (usize i = 0; i < arg.size() && !needs_quotes; ++i) {
        const char c = arg[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '"') needs_quotes = true;
    }
    if (!needs_quotes) {
        cmdline.append(arg);
        return;
    }
    cmdline.push_back('"');
    usize backslashes = 0;
    for (usize i = 0; i < arg.size(); ++i) {
        const char c = arg[i];
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            for (usize k = 0; k < backslashes * 2 + 1; ++k) cmdline.push_back('\\');
            cmdline.push_back('"');
            backslashes = 0;
        } else {
            for (usize k = 0; k < backslashes; ++k) cmdline.push_back('\\');
            backslashes = 0;
            cmdline.push_back(c);
        }
    }
    for (usize k = 0; k < backslashes * 2; ++k) cmdline.push_back('\\');
    cmdline.push_back('"');
}

struct WinPipeReaderCtx {
    HANDLE pipe = nullptr;
    String* dst = nullptr;
    usize max_bytes = 0;
};

static DWORD WINAPI win_pipe_reader_thread(LPVOID param) {
    auto* ctx = static_cast<WinPipeReaderCtx*>(param);
    char buf[4096];
    DWORD nread = 0;
    while (ReadFile(ctx->pipe, buf, sizeof(buf), &nread, nullptr) && nread > 0) {
        if (ctx->max_bytes == 0 || ctx->dst->size() < ctx->max_bytes) {
            usize to_add = static_cast<usize>(nread);
            if (ctx->max_bytes != 0 && ctx->dst->size() + to_add > ctx->max_bytes) {
                to_add = ctx->max_bytes - ctx->dst->size();
            }
            ctx->dst->append(StringRef(buf, to_add));
        }
    }
    return 0;
}

static bool build_win32_env_block(ArrayRef<ProcessEnvVar> overrides, Array<wchar_t>& block) {
    if (overrides.empty()) return true;
    LPWCH cur_env = GetEnvironmentStringsW();
    if (!cur_env) return false;

    // Collect existing entries, omitting overridden names (case-insensitive on Windows).
    Array<Array<wchar_t>> override_names;
    Array<Array<wchar_t>> override_entries;
    override_names.reserve(overrides.size());
    override_entries.reserve(overrides.size());
    for (usize i = 0; i < overrides.size(); ++i) {
        Array<wchar_t> wname;
        if (!proc_utf8_to_wide(overrides[i].name, wname)) {
            FreeEnvironmentStringsW(cur_env);
            return false;
        }
        String kv;
        kv.reserve(overrides[i].name.size() + 1 + overrides[i].value.size());
        kv.append(overrides[i].name);
        kv.push_back('=');
        kv.append(overrides[i].value);
        Array<wchar_t> wkv;
        if (!proc_utf8_to_wide(kv, wkv)) {
            FreeEnvironmentStringsW(cur_env);
            return false;
        }
        override_names.push_back(keel::move(wname));
        override_entries.push_back(keel::move(wkv));
    }

    auto is_overridden = [&](const wchar_t* entry, usize eq_pos) -> bool {
        for (usize i = 0; i < override_names.size(); ++i) {
            const usize nlen = override_names[i].size() - 1;
            if (nlen == eq_pos && _wcsnicmp(entry, override_names[i].data(), eq_pos) == 0) {
                return true;
            }
        }
        return false;
    };

    block.clear();
    const wchar_t* p = cur_env;
    while (*p != L'\0') {
        usize len = 0;
        usize eq_pos = kInvalidIndex;
        while (p[len] != L'\0') {
            if (p[len] == L'=' && eq_pos == kInvalidIndex && len > 0) eq_pos = len;
            ++len;
        }
        if (eq_pos == kInvalidIndex || !is_overridden(p, eq_pos)) {
            for (usize k = 0; k <= len; ++k) block.push_back(p[k]);
        }
        p += len + 1;
    }
    FreeEnvironmentStringsW(cur_env);

    for (usize i = 0; i < override_entries.size(); ++i) {
        const auto& e = override_entries[i];
        for (usize k = 0; k < e.size(); ++k) block.push_back(e[k]);
    }
    block.push_back(L'\0');
    return true;
}

static Result<ProcessOutput> run_win32_cmdline(StringRef cmdline_utf8, const ProcessOptions& opts, Allocator* alloc) {
    Allocator* a = alloc ? alloc : default_allocator();
    Array<wchar_t> wcmd;
    if (!proc_utf8_to_wide(cmdline_utf8, wcmd)) {
        return Error{ErrorCode::InvalidData, 0};
    }

    SmallArray<wchar_t, 260> wcwd;
    const wchar_t* cwd_ptr = nullptr;
    if (!opts.cwd.empty()) {
        if (!utf8_path_to_win32_wide(opts.cwd, wcwd)) {
            return Error{ErrorCode::InvalidArgument, 0};
        }
        cwd_ptr = wcwd.data();
    }

    Array<wchar_t> env_block;
    void* env_ptr = nullptr;
    DWORD creation_flags = 0;
    if (!opts.env.empty()) {
        if (!build_win32_env_block(opts.env, env_block)) {
            return Error{ErrorCode::InvalidData, 0};
        }
        env_ptr = env_block.data();
        creation_flags |= CREATE_UNICODE_ENVIRONMENT;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE out_read = nullptr, out_write = nullptr;
    HANDLE err_read = nullptr, err_write = nullptr;
    const bool need_out = opts.capture_stdout || opts.merge_stderr;
    const bool need_err = opts.capture_stderr && !opts.merge_stderr;

    auto close_pipes = [&]() {
        if (out_read) CloseHandle(out_read);
        if (out_write) CloseHandle(out_write);
        if (err_read) CloseHandle(err_read);
        if (err_write) CloseHandle(err_write);
    };

    if (need_out) {
        if (!CreatePipe(&out_read, &out_write, &sa, 0)) {
            const DWORD err = GetLastError();
            close_pipes();
            return win32_error_to_keel(err);
        }
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    }
    if (need_err) {
        if (!CreatePipe(&err_read, &err_write, &sa, 0)) {
            const DWORD err = GetLastError();
            close_pipes();
            return win32_error_to_keel(err);
        }
        SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (need_out || need_err) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = need_out ? out_write : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = opts.merge_stderr ? out_write : (need_err ? err_write : GetStdHandle(STD_ERROR_HANDLE));
    }

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, (need_out || need_err) ? TRUE : FALSE,
                                   creation_flags, env_ptr, cwd_ptr, &si, &pi);
    if (!ok) {
        const DWORD err = GetLastError();
        close_pipes();
        return win32_error_to_keel(err);
    }

    if (out_write) {
        CloseHandle(out_write);
        out_write = nullptr;
    }
    if (err_write) {
        CloseHandle(err_write);
        err_write = nullptr;
    }

    ProcessOutput result{-1, String(a), String(a)};

    HANDLE err_thread = nullptr;
    WinPipeReaderCtx err_ctx{err_read, &result.stderr_text, opts.max_output_bytes};
    if (err_read) {
        err_thread = CreateThread(nullptr, 0, win_pipe_reader_thread, &err_ctx, 0, nullptr);
    }

    if (out_read) {
        WinPipeReaderCtx out_ctx{out_read, &result.stdout_text, opts.max_output_bytes};
        win_pipe_reader_thread(&out_ctx);
        CloseHandle(out_read);
        out_read = nullptr;
    }

    if (err_thread) {
        WaitForSingleObject(err_thread, INFINITE);
        CloseHandle(err_thread);
    } else if (err_read) {
        win_pipe_reader_thread(&err_ctx);
    }
    if (err_read) {
        CloseHandle(err_read);
        err_read = nullptr;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = static_cast<i32>(exit_code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
}
} // namespace detail

Result<ProcessOutput> process_run(ArrayRef<StringRef> argv, const ProcessOptions& opts, Allocator* alloc) {
    if (argv.empty() || argv[0].empty()) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    String cmdline;
    for (usize i = 0; i < argv.size(); ++i) {
        if (i > 0) cmdline.push_back(' ');
        detail::append_win32_arg(cmdline, argv[i]);
    }
    return detail::run_win32_cmdline(cmdline, opts, alloc);
}

Result<ProcessOutput> process_run_shell(StringRef command, const ProcessOptions& opts, Allocator* alloc) {
    if (command.empty()) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    String full_cmd;
    full_cmd.reserve(command.size() + 16);
    full_cmd.append("cmd.exe /c \"");
    full_cmd.append(command);
    full_cmd.push_back('"');
    return detail::run_win32_cmdline(full_cmd, opts, alloc);
}

Optional<String> process_which(StringRef program, Allocator* alloc) {
    if (program.empty()) return kNullOpt;
    for (usize i = 0; i < program.size(); ++i) {
        const auto ch = static_cast<u8>(program[i]);
        if (ch < 0x20u || ch == 0x7Fu) return kNullOpt;
    }
    Allocator* a = alloc ? alloc : default_allocator();

    // Build PATHEXT list
    SmallArray<String, 8> exts;
    Optional<String> pathext = env_get("PATHEXT");
    const StringRef ext_list = pathext.has_value() && !pathext->empty()
                                   ? pathext->as_ref()
                                   : StringRef(".COM;.EXE;.BAT;.CMD");
    usize pos = 0;
    while (pos <= ext_list.size()) {
        const usize sep = ext_list.find(';', pos);
        const StringRef tok = (sep == kInvalidIndex) ? ext_list.substr(pos) : ext_list.substr(pos, sep - pos);
        if (!tok.empty()) exts.push_back(String::from(tok));
        if (sep == kInvalidIndex) break;
        pos = sep + 1;
    }

    auto try_candidate = [&](StringRef cand) -> Optional<String> {
        if (fs_is_file(cand)) return String::from(cand, a);
        if (path_extension(cand).empty()) {
            for (usize i = 0; i < exts.size(); ++i) {
                String with_ext(a);
                with_ext.reserve(cand.size() + exts[i].size());
                with_ext.append(cand);
                with_ext.append(exts[i]);
                if (fs_is_file(with_ext)) return with_ext;
            }
        }
        return kNullOpt;
    };

    if (program.contains('/') || program.contains('\\')) {
        return try_candidate(program);
    }

    Optional<String> path_env = env_get("PATH");
    if (!path_env.has_value() || path_env->empty()) return kNullOpt;

    const StringRef path_ref = path_env->as_ref();
    pos = 0;
    while (pos <= path_ref.size()) {
        const usize sep = path_ref.find(';', pos);
        StringRef dir = (sep == kInvalidIndex) ? path_ref.substr(pos) : path_ref.substr(pos, sep - pos);
        if (dir.size() >= 2 && dir.front() == '"' && dir.back() == '"') {
            dir = dir.substr(1, dir.size() - 2);
        }
        if (!dir.empty()) {
            String cand = path_join(dir, program, a);
            if (auto found = try_candidate(cand); found.has_value()) return found;
        }
        if (sep == kInvalidIndex) break;
        pos = sep + 1;
    }
    return kNullOpt;
}

} // namespace keel

#else // POSIX

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace keel {

namespace detail {
Error errno_to_keel(int err) noexcept;

static bool set_cloexec(int fd) noexcept {
    int flags = ::fcntl(fd, F_GETFD);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

static bool create_cloexec_pipe(int fds[2]) noexcept {
    if (::pipe(fds) != 0) return false;
    if (!set_cloexec(fds[0]) || !set_cloexec(fds[1])) {
        ::close(fds[0]);
        ::close(fds[1]);
        fds[0] = fds[1] = -1;
        return false;
    }
    return true;
}

static bool is_executable_file_posix(const char* path) noexcept {
    struct stat st {};
    if (::stat(path, &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    return ::access(path, X_OK) == 0;
}
} // namespace detail

Result<ProcessOutput> process_run(ArrayRef<StringRef> argv, const ProcessOptions& opts, Allocator* alloc) {
    if (argv.empty() || argv[0].empty()) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    Allocator* a = alloc ? alloc : default_allocator();

    // Prepare null-terminated strings BEFORE fork() to avoid heap allocations in the child process.
    Array<String> arg_storage;
    arg_storage.reserve(argv.size());
    Array<char*> c_argv;
    c_argv.reserve(argv.size() + 1);
    for (usize i = 0; i < argv.size(); ++i) {
        arg_storage.push_back(String::from(argv[i]));
    }
    for (usize i = 0; i < arg_storage.size(); ++i) {
        c_argv.push_back(const_cast<char*>(arg_storage[i].c_str()));
    }
    c_argv.push_back(nullptr);

    String cwd_storage;
    const char* c_cwd = nullptr;
    if (!opts.cwd.empty()) {
        cwd_storage = String::from(opts.cwd);
        c_cwd = cwd_storage.c_str();
    }

    Array<String> env_names;
    Array<String> env_vals;
    if (!opts.env.empty()) {
        env_names.reserve(opts.env.size());
        env_vals.reserve(opts.env.size());
        for (usize i = 0; i < opts.env.size(); ++i) {
            if (opts.env[i].name.empty()) return Error{ErrorCode::InvalidArgument, 0};
            env_names.push_back(String::from(opts.env[i].name));
            env_vals.push_back(String::from(opts.env[i].value));
        }
    }

    const bool need_out = opts.capture_stdout || opts.merge_stderr;
    const bool need_err = opts.capture_stderr && !opts.merge_stderr;

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    int exec_pipe[2] = {-1, -1}; // Reports child exec/chdir errno back to parent

    auto close_all_pipes = [&]() {
        if (out_pipe[0] >= 0) ::close(out_pipe[0]);
        if (out_pipe[1] >= 0) ::close(out_pipe[1]);
        if (err_pipe[0] >= 0) ::close(err_pipe[0]);
        if (err_pipe[1] >= 0) ::close(err_pipe[1]);
        if (exec_pipe[0] >= 0) ::close(exec_pipe[0]);
        if (exec_pipe[1] >= 0) ::close(exec_pipe[1]);
    };

    if (need_out && !detail::create_cloexec_pipe(out_pipe)) {
        const int err = errno;
        close_all_pipes();
        return detail::errno_to_keel(err);
    }
    if (need_err && !detail::create_cloexec_pipe(err_pipe)) {
        const int err = errno;
        close_all_pipes();
        return detail::errno_to_keel(err);
    }
    if (!detail::create_cloexec_pipe(exec_pipe)) {
        const int err = errno;
        close_all_pipes();
        return detail::errno_to_keel(err);
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int err = errno;
        close_all_pipes();
        return detail::errno_to_keel(err);
    }

    if (pid == 0) {
        // Child process
        ::close(exec_pipe[0]);
        if (need_out) {
            ::close(out_pipe[0]);
            while (::dup2(out_pipe[1], STDOUT_FILENO) < 0 && errno == EINTR) {}
            if (opts.merge_stderr) {
                while (::dup2(out_pipe[1], STDERR_FILENO) < 0 && errno == EINTR) {}
            }
            ::close(out_pipe[1]);
        }
        if (need_err) {
            ::close(err_pipe[0]);
            while (::dup2(err_pipe[1], STDERR_FILENO) < 0 && errno == EINTR) {}
            ::close(err_pipe[1]);
        }

        if (c_cwd && ::chdir(c_cwd) != 0) {
            int child_err = errno;
            const ssize_t ignored = ::write(exec_pipe[1], &child_err, sizeof(child_err));
            (void)ignored;
            ::_exit(127);
        }

        for (usize i = 0; i < env_names.size(); ++i) {
            ::setenv(env_names[i].c_str(), env_vals[i].c_str(), 1);
        }

        ::execvp(c_argv[0], c_argv.data());
        int child_err = errno;
        const ssize_t ignored = ::write(exec_pipe[1], &child_err, sizeof(child_err));
        (void)ignored;
        ::_exit(child_err == ENOENT ? 127 : 126);
    }

    // Parent process
    ::close(exec_pipe[1]);
    if (out_pipe[1] >= 0) ::close(out_pipe[1]);
    if (err_pipe[1] >= 0) ::close(err_pipe[1]);

    // Check if child reported a pre-exec/exec failure via CLOEXEC pipe.
    int child_exec_errno = 0;
    ssize_t er = 0;
    do {
        er = ::read(exec_pipe[0], &child_exec_errno, sizeof(child_exec_errno));
    } while (er < 0 && errno == EINTR);
    ::close(exec_pipe[0]);

    if (er == static_cast<ssize_t>(sizeof(child_exec_errno))) {
        if (out_pipe[0] >= 0) ::close(out_pipe[0]);
        if (err_pipe[0] >= 0) ::close(err_pipe[0]);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        return detail::errno_to_keel(child_exec_errno);
    }

    ProcessOutput result{-1, String(a), String(a)};

    // Drain stdout and stderr concurrently with poll() so full pipe buffers cannot deadlock.
    int read_fds[2] = {out_pipe[0], err_pipe[0]};
    String* dsts[2] = {&result.stdout_text, &result.stderr_text};
    char buf[4096];

    while (read_fds[0] >= 0 || read_fds[1] >= 0) {
        struct pollfd pfds[2]{};
        int map_idx[2] = {-1, -1};
        nfds_t nfds = 0;
        for (int i = 0; i < 2; ++i) {
            if (read_fds[i] >= 0) {
                pfds[nfds].fd = read_fds[i];
                pfds[nfds].events = POLLIN | POLLHUP | POLLERR;
                map_idx[nfds] = i;
                ++nfds;
            }
        }
        const int pr = ::poll(pfds, nfds, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (nfds_t k = 0; k < nfds; ++k) {
            if (pfds[k].revents == 0) continue;
            const int slot = map_idx[k];
            const ssize_t n = ::read(read_fds[slot], buf, sizeof(buf));
            if (n > 0) {
                String& dst = *dsts[slot];
                if (opts.max_output_bytes == 0 || dst.size() < opts.max_output_bytes) {
                    usize to_add = static_cast<usize>(n);
                    if (opts.max_output_bytes != 0 && dst.size() + to_add > opts.max_output_bytes) {
                        to_add = opts.max_output_bytes - dst.size();
                    }
                    dst.append(StringRef(buf, to_add));
                }
            } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
                ::close(read_fds[slot]);
                read_fds[slot] = -1;
            }
        }
    }
    if (read_fds[0] >= 0) ::close(read_fds[0]);
    if (read_fds[1] >= 0) ::close(read_fds[1]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return detail::errno_to_keel(errno);
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = -1;
    }
    return result;
}

Result<ProcessOutput> process_run_shell(StringRef command, const ProcessOptions& opts, Allocator* alloc) {
    if (command.empty()) {
        return Error{ErrorCode::InvalidArgument, 0};
    }
    const StringRef argv[3] = {"/bin/sh", "-c", command};
    return process_run(ArrayRef<StringRef>(argv, 3), opts, alloc);
}

Optional<String> process_which(StringRef program, Allocator* alloc) {
    if (program.empty() || program[0] == '-') return kNullOpt;
    for (usize i = 0; i < program.size(); ++i) {
        const auto ch = static_cast<u8>(program[i]);
        if (ch < 0x20u || ch == 0x7Fu) return kNullOpt;
    }
    Allocator* a = alloc ? alloc : default_allocator();

    if (program.contains('/')) {
        SmallString<256> p(program);
        if (detail::is_executable_file_posix(p.c_str())) {
            return String::from(program, a);
        }
        return kNullOpt;
    }

    Optional<String> path_env = env_get("PATH");
    if (!path_env.has_value() || path_env->empty()) return kNullOpt;

    const StringRef path_ref = path_env->as_ref();
    usize pos = 0;
    while (pos <= path_ref.size()) {
        const usize sep = path_ref.find(':', pos);
        const StringRef dir = (sep == kInvalidIndex) ? path_ref.substr(pos) : path_ref.substr(pos, sep - pos);
        const StringRef effective_dir = dir.empty() ? StringRef(".") : dir;
        String cand = path_join(effective_dir, program, a);
        if (detail::is_executable_file_posix(cand.c_str())) {
            return cand;
        }
        if (sep == kInvalidIndex) break;
        pos = sep + 1;
    }
    return kNullOpt;
}

} // namespace keel

#endif
