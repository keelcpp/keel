#include "keel/sysinfo.h"
#include "keel/arena.h"
#include "keel/env.h"
#include "keel/fs.h"
#include "keel/number.h"
#include "keel/small_array.h"
#include "keel/thread_pool.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#elif defined(_WIN32)
#include "win32_compat.h"
#else
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif
#endif

namespace keel {

u32 sys_cpu_count() noexcept { return hardware_concurrency(); }

#if defined(__EMSCRIPTEN__)

namespace detail::sysinfo_impl {

EM_JS(f64, keel_js_heap_bytes, (), {
    if (typeof HEAPU8 !== 'undefined' && HEAPU8.buffer) return HEAPU8.buffer.byteLength;
    return 0;
});

EM_JS(usize, keel_js_get_locale, (char* out, usize cap), {
    if (typeof navigator === 'undefined' || !navigator.language) return 0;
    var bytes = new TextEncoder().encode(navigator.language);
    var n = Math.min(bytes.length, cap);
    HEAPU8.set(bytes.subarray(0, n), out);
    return n;
});

} // namespace detail::sysinfo_impl

usize sys_page_size() noexcept { return 65536u; }

u64 sys_total_ram_bytes() noexcept { return static_cast<u64>(detail::sysinfo_impl::keel_js_heap_bytes()); }

u64 sys_available_ram_bytes() noexcept { return 0ULL; }

String sys_os_version(Allocator* alloc) { return String::from("Web", alloc); }

String sys_locale(Allocator* alloc) {
    char buf[64]{};
    const usize n = detail::sysinfo_impl::keel_js_get_locale(buf, sizeof(buf));
    return String::from(StringRef(buf, n), alloc);
}

#elif defined(_WIN32)

usize sys_page_size() noexcept {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return (si.dwPageSize > 0) ? as<usize>(si.dwPageSize) : 4096u;
}

u64 sys_total_ram_bytes() noexcept {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return static_cast<u64>(ms.ullTotalPhys);
    }
    return 0ULL;
}

u64 sys_available_ram_bytes() noexcept {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return static_cast<u64>(ms.ullAvailPhys);
    }
    return 0ULL;
}

String sys_os_version(Allocator* alloc) {
    struct RtlOsVersionInfoW {
        ULONG dwOSVersionInfoSize;
        ULONG dwMajorVersion;
        ULONG dwMinorVersion;
        ULONG dwBuildNumber;
        ULONG dwPlatformId;
        wchar_t szCSDVersion[128];
    };
    using PfnRtlGetVersion = LONG(WINAPI*)(RtlOsVersionInfoW*);

    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto rtl_get = reinterpret_cast<PfnRtlGetVersion>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtl_get) {
            RtlOsVersionInfoW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (rtl_get(&info) == 0) {
                String out(alloc);
                char buf[32]{};
                out.append(format_u64_to(buf, info.dwMajorVersion));
                out.push_back('.');
                out.append(format_u64_to(buf, info.dwMinorVersion));
                out.push_back('.');
                out.append(format_u64_to(buf, info.dwBuildNumber));
                return out;
            }
        }
    }
    return String::from("Windows", alloc);
}

String sys_locale(Allocator* alloc) {
    wchar_t wloc[LOCALE_NAME_MAX_LENGTH]{};
    const int n = GetUserDefaultLocaleName(wloc, LOCALE_NAME_MAX_LENGTH);
    if (n <= 1) {
        return String(alloc);
    }
    const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wloc, n - 1, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) {
        return String(alloc);
    }
    String out(alloc);
    out.resize(as<usize>(utf8_len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wloc, n - 1, out.data(), utf8_len, nullptr, nullptr);
    return out;
}

#else // POSIX (Linux, macOS, BSD)

namespace detail::sysinfo_impl {

#if defined(__linux__)
inline u64 parse_meminfo_kb(StringRef content, StringRef key) noexcept {
    usize pos = 0;
    while (pos < content.size()) {
        usize end = content.find('\n', pos);
        if (end == kInvalidIndex) end = content.size();
        const StringRef line = content.substr(pos, end - pos);
        pos = (end < content.size()) ? (end + 1u) : content.size();
        if (line.size() > key.size() && line.substr(0, key.size()) == key) {
            usize p = key.size();
            while (p < line.size() && (line[p] == ' ' || line[p] == '\t' || line[p] == ':')) {
                ++p;
            }
            usize d_end = p;
            while (d_end < line.size() && line[d_end] >= '0' && line[d_end] <= '9') {
                ++d_end;
            }
            if (d_end > p) {
                if (auto val = parse_u64(line.substr(p, d_end - p)); val.is_ok()) {
                    return *val * 1024ULL;
                }
            }
        }
    }
    return 0ULL;
}
#endif

} // namespace detail::sysinfo_impl

usize sys_page_size() noexcept {
    const long ps = ::sysconf(_SC_PAGESIZE);
    return (ps > 0) ? static_cast<usize>(ps) : 4096u;
}

u64 sys_total_ram_bytes() noexcept {
#if defined(__APPLE__)
    u64 mem = 0;
    usize len = sizeof(mem);
    if (::sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) {
        return mem;
    }
    return 0ULL;
#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    const long pages = ::sysconf(_SC_PHYS_PAGES);
    const long ps = ::sysconf(_SC_PAGESIZE);
    if (pages > 0 && ps > 0) {
        return static_cast<u64>(pages) * static_cast<u64>(ps);
    }
    return 0ULL;
#else
    return 0ULL;
#endif
}

u64 sys_available_ram_bytes() noexcept {
#if defined(__linux__)
    ArenaTemp scratch = get_scratch();
    if (auto txt = fs_read_text("/proc/meminfo", scratch.allocator()); txt.is_ok()) {
        const u64 avail = detail::sysinfo_impl::parse_meminfo_kb(*txt, "MemAvailable");
        if (avail > 0ULL) return avail;
        const u64 free_b = detail::sysinfo_impl::parse_meminfo_kb(*txt, "MemFree");
        if (free_b > 0ULL) return free_b;
    }
#elif defined(__APPLE__)
    vm_statistics64_data_t vm_stat{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm_stat), &count) ==
        KERN_SUCCESS) {
        const u64 ps = static_cast<u64>(sys_page_size());
        return (static_cast<u64>(vm_stat.free_count) + static_cast<u64>(vm_stat.inactive_count)) * ps;
    }
#endif
#if defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
    const long pages = ::sysconf(_SC_AVPHYS_PAGES);
    const long ps = ::sysconf(_SC_PAGESIZE);
    if (pages > 0 && ps > 0) {
        return static_cast<u64>(pages) * static_cast<u64>(ps);
    }
#endif
    return 0ULL;
}

String sys_os_version(Allocator* alloc) {
#if defined(__APPLE__)
    char buf[64]{};
    usize len = sizeof(buf) - 1u;
    if (::sysctlbyname("kern.osproductversion", buf, &len, nullptr, 0) == 0 && len > 0) {
        if (buf[len - 1u] == '\0') --len;
        return String::from(StringRef(buf, len), alloc);
    }
#endif
    struct utsname u{};
    if (::uname(&u) == 0) {
        return String::from(StringRef(u.release), alloc);
    }
    return String(alloc);
}

String sys_locale(Allocator* alloc) {
    // POSIX locale precedence: first non-empty variable among LC_ALL, LC_MESSAGES, LANG wins (including "C"/"POSIX").
    static constexpr const char* kEnvKeys[3] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (usize i = 0; i < 3; ++i) {
        if (auto val = env_get(kEnvKeys[i], alloc); val.has_value() && !val->empty()) {
            StringRef raw = val->as_ref();
            // Strip encoding / modifier suffix (e.g. "en_US.UTF-8@euro" -> "en_US")
            const usize dot = raw.find('.');
            if (dot != kInvalidIndex) raw = raw.substr(0, dot);
            const usize at = raw.find('@');
            if (at != kInvalidIndex) raw = raw.substr(0, at);
            if (!raw.empty()) {
                return String::from(raw, alloc);
            }
        }
    }
    return String::from("C", alloc);
}

#endif

} // namespace keel
