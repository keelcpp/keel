#include "keel/fs_watch.h"
#include "keel/arena.h"
#include "keel/array.h"
#include "keel/fs.h"
#include "keel/small_array.h"

#if defined(__EMSCRIPTEN__)
#define KEEL_FSWATCH_BACKEND_NONE 1
#elif defined(_WIN32)
#define KEEL_FSWATCH_BACKEND_WIN32 1
#include "win32_compat.h"
#elif defined(__linux__)
#define KEEL_FSWATCH_BACKEND_INOTIFY 1
#include <cerrno>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define KEEL_FSWATCH_BACKEND_KQUEUE 1
#include <cerrno>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#else
#define KEEL_FSWATCH_BACKEND_NONE 1
#endif

namespace keel {

namespace detail {
#if defined(_WIN32)
Error win32_error_to_keel(DWORD err) noexcept;
bool utf8_path_to_win32_wide(StringRef utf8_path, SmallArray<wchar_t, 260>& out);
void win32_wide_to_utf8_path(const wchar_t* wstr, usize wlen, String& out);
#elif !defined(KEEL_FSWATCH_BACKEND_NONE)
Error errno_to_keel(int err) noexcept;
#endif
} // namespace detail

#if defined(KEEL_FSWATCH_BACKEND_NONE)

struct FileWatcher::Impl {};

Result<FileWatcher> FileWatcher::create(Allocator*) { return Error{ErrorCode::NotSupported, 0}; }
Error FileWatcher::watch_dir(StringRef, bool) { return Error{ErrorCode::NotSupported, 0}; }
Error FileWatcher::unwatch_dir(StringRef) noexcept { return Error{ErrorCode::NotSupported, 0}; }
Error FileWatcher::poll(u64, FunctionRef<bool(const WatchEvent&)>) { return Error{ErrorCode::NotSupported, 0}; }
void FileWatcher::close() noexcept {
    m_impl = nullptr;
    m_alloc = nullptr;
}

#elif defined(KEEL_FSWATCH_BACKEND_INOTIFY)

struct FileWatcher::Impl {
    struct Entry {
        int wd = -1;
        String dir;
        bool recursive = false;
    };

    int fd = -1;
    Allocator* alloc = nullptr;
    Array<Entry> entries;
    // Persistent read buffer so returning false from poll() callback never discards queued events.
    alignas(struct inotify_event) u8 pending_buf[8192]{};
    usize pending_off = 0;
    usize pending_len = 0;

    explicit Impl(int inotify_fd, Allocator* a) : fd(inotify_fd), alloc(a), entries(a) {}

    ~Impl() noexcept {
        if (fd >= 0) {
            (void)::close(fd);
            fd = -1;
        }
    }

    Error add_single(StringRef norm_dir, bool recursive) {
        for (usize i = 0; i < entries.size(); ++i) {
            if (entries[i].dir.as_ref() == norm_dir) {
                entries[i].recursive = entries[i].recursive || recursive;
                return Error::ok();
            }
        }
        static constexpr u32 kMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM |
                                     IN_MOVED_TO | IN_ATTRIB | IN_DELETE_SELF;
        String cdir = String::from(norm_dir, alloc);
        const int wd = ::inotify_add_watch(fd, cdir.c_str(), kMask);
        if (wd < 0) {
            return detail::errno_to_keel(errno);
        }
        entries.push_back(Entry{wd, keel::move(cdir), recursive});
        return Error::ok();
    }

    usize find_index_by_wd(int wd) const noexcept {
        for (usize i = 0; i < entries.size(); ++i) {
            if (entries[i].wd == wd) return i;
        }
        return kInvalidIndex;
    }

    bool drain_pending(FunctionRef<bool(const WatchEvent&)> callback) {
        bool keep_going = true;
        while (pending_off + sizeof(struct inotify_event) <= pending_len && keep_going) {
            struct inotify_event ev{};
            mem_copy(&ev, pending_buf + pending_off, sizeof(ev));
            const usize step = sizeof(struct inotify_event) + as<usize>(ev.len);
            if (pending_off + step > pending_len) {
                pending_off = pending_len;
                break;
            }

            const char* raw_name =
                (ev.len > 0) ? reinterpret_cast<const char*>(pending_buf + pending_off + sizeof(ev)) : "";
            usize name_len = 0;
            while (name_len < as<usize>(ev.len) && raw_name[name_len] != '\0') ++name_len;
            const StringRef name(raw_name, name_len);
            pending_off += step;

            const usize idx = find_index_by_wd(ev.wd);
            if (idx == kInvalidIndex) continue;

            if ((ev.mask & IN_IGNORED) != 0) {
                entries.erase(idx);
                continue;
            }

            const bool is_recursive = entries[idx].recursive;
            ArenaTemp scratch = get_scratch(alloc);
            const String full = name.empty() ? String::from(entries[idx].dir.as_ref(), scratch.allocator())
                                             : path_join(entries[idx].dir.as_ref(), name, scratch.allocator());

            if (is_recursive && ((ev.mask & (IN_CREATE | IN_ISDIR)) == (IN_CREATE | IN_ISDIR))) {
                (void)add_single(full.as_ref(), true);
            }

            WatchEventKind kind = WatchEventKind::Modified;
            if ((ev.mask & IN_CREATE) != 0) {
                kind = WatchEventKind::Added;
            } else if ((ev.mask & (IN_DELETE | IN_DELETE_SELF)) != 0) {
                kind = WatchEventKind::Removed;
            } else if ((ev.mask & (IN_MOVED_FROM | IN_MOVED_TO)) != 0) {
                kind = WatchEventKind::Renamed;
            } else if ((ev.mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB)) != 0) {
                kind = WatchEventKind::Modified;
            } else {
                continue;
            }

            const WatchEvent we{kind, full.as_ref()};
            if (!callback(we)) {
                keep_going = false;
            }
        }
        if (pending_off >= pending_len) {
            pending_off = 0;
            pending_len = 0;
        }
        return keep_going;
    }
};

Result<FileWatcher> FileWatcher::create(Allocator* alloc) {
    Allocator* a = alloc ? alloc : default_allocator();
    const int fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return detail::errno_to_keel(errno);
    }
    void* mem = a->try_alloc_bytes(sizeof(Impl), alignof(Impl));
    if (!mem) {
        (void)::close(fd);
        return Error{ErrorCode::OutOfRange, 0};
    }
    FileWatcher out;
    out.m_impl = ::new (mem) Impl(fd, a);
    out.m_alloc = a;
    return out;
}

Error FileWatcher::watch_dir(StringRef dir_path, bool recursive) {
    if (!m_impl) return Error{ErrorCode::InvalidArgument, 0};
    if (dir_path.empty() || !fs_is_dir(dir_path)) return Error{ErrorCode::NotFound, 0};

    ArenaTemp scratch = get_scratch(m_alloc);
    String norm = path_normalize(dir_path, scratch.allocator());
    const Error root_err = m_impl->add_single(norm.as_ref(), recursive);
    if (root_err.is_err()) return root_err;

    if (recursive) {
        (void)fs_walk_dir(norm.as_ref(), [&](const DirEntry& ent) {
            if (ent.is_dir) {
                String sub = path_normalize(ent.full_path, scratch.allocator());
                (void)m_impl->add_single(sub.as_ref(), true);
            }
            return true;
        });
    }
    return Error::ok();
}

Error FileWatcher::unwatch_dir(StringRef dir_path) noexcept {
    if (!m_impl || dir_path.empty()) return Error{ErrorCode::InvalidArgument, 0};
    ArenaTemp scratch = get_scratch(m_alloc);
    const String norm = path_normalize(dir_path, scratch.allocator());
    bool found = false;
    usize i = 0;
    while (i < m_impl->entries.size()) {
        const StringRef cur = m_impl->entries[i].dir.as_ref();
        const bool is_sub =
            (norm.as_ref() == "/")
                ? cur.starts_with('/')
                : (cur.size() > norm.size() && cur.starts_with(norm.as_ref()) && cur[norm.size()] == '/');
        if (cur == norm.as_ref() || is_sub) {
            (void)::inotify_rm_watch(m_impl->fd, m_impl->entries[i].wd);
            m_impl->entries.erase(i);
            found = true;
        } else {
            ++i;
        }
    }
    return found ? Error::ok() : Error{ErrorCode::NotFound, 0};
}

Error FileWatcher::poll(u64 timeout_ms, FunctionRef<bool(const WatchEvent&)> callback) {
    if (!m_impl || !callback) return Error{ErrorCode::InvalidArgument, 0};

    // First deliver any queued events left over from an earlier callback stop.
    if (m_impl->pending_off < m_impl->pending_len) {
        if (!m_impl->drain_pending(callback)) {
            return Error::ok();
        }
    }

    struct pollfd pfd{};
    pfd.fd = m_impl->fd;
    pfd.events = POLLIN;
    const int wait_ms = (timeout_ms > 0x7FFFFFFFULL) ? 0x7FFFFFFF : as<int>(timeout_ms);
    int pr = 0;
    do {
        pr = ::poll(&pfd, 1, wait_ms);
    } while (pr < 0 && errno == EINTR);
    if (pr < 0) return detail::errno_to_keel(errno);
    if (pr == 0 || (pfd.revents & POLLIN) == 0) return Error::ok();

    for (;;) {
        const ssize_t n = ::read(m_impl->fd, m_impl->pending_buf, sizeof(m_impl->pending_buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return detail::errno_to_keel(errno);
        }
        if (n == 0) break;
        m_impl->pending_off = 0;
        m_impl->pending_len = as<usize>(n);
        if (!m_impl->drain_pending(callback)) {
            break;
        }
    }
    return Error::ok();
}

void FileWatcher::close() noexcept {
    if (m_impl && m_alloc) {
        m_impl->~Impl();
        m_alloc->free_bytes(m_impl, sizeof(Impl), alignof(Impl));
    }
    m_impl = nullptr;
    m_alloc = nullptr;
}

#elif defined(KEEL_FSWATCH_BACKEND_WIN32)

struct FileWatcher::Impl {
    struct Entry {
        HANDLE dir_handle = INVALID_HANDLE_VALUE;
        HANDLE ov_event = nullptr;
        OVERLAPPED ov{};
        String dir;
        bool recursive = false;
        bool pending = false;
        alignas(4) u8 buf[16384]{};
    };

    Allocator* alloc = nullptr;
    Array<Entry*> entries;

    explicit Impl(Allocator* a) : alloc(a), entries(a) {}

    ~Impl() noexcept {
        for (usize i = 0; i < entries.size(); ++i) {
            destroy_entry(entries[i]);
        }
        entries.clear();
    }

    void destroy_entry(Entry* e) noexcept {
        if (!e) return;
        if (e->dir_handle != INVALID_HANDLE_VALUE) {
            if (e->pending) {
                CancelIo(e->dir_handle);
                DWORD ignored = 0;
                (void)GetOverlappedResult(e->dir_handle, &e->ov, &ignored, TRUE);
            }
            CloseHandle(e->dir_handle);
        }
        if (e->ov_event) {
            CloseHandle(e->ov_event);
        }
        e->~Entry();
        alloc->free_bytes(e, sizeof(Entry), alignof(Entry));
    }

    bool arm(Entry* e) noexcept {
        ResetEvent(e->ov_event);
        mem_zero(&e->ov, sizeof(e->ov));
        e->ov.hEvent = e->ov_event;
        static constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                         FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
        DWORD ignored = 0;
        const BOOL ok = ReadDirectoryChangesW(e->dir_handle, e->buf, sizeof(e->buf), e->recursive ? TRUE : FALSE,
                                              kFilter, &ignored, &e->ov, nullptr);
        e->pending = (ok != 0);
        return e->pending;
    }
};

Result<FileWatcher> FileWatcher::create(Allocator* alloc) {
    Allocator* a = alloc ? alloc : default_allocator();
    void* mem = a->try_alloc_bytes(sizeof(Impl), alignof(Impl));
    if (!mem) return Error{ErrorCode::OutOfRange, 0};
    FileWatcher out;
    out.m_impl = ::new (mem) Impl(a);
    out.m_alloc = a;
    return out;
}

Error FileWatcher::watch_dir(StringRef dir_path, bool recursive) {
    if (!m_impl) return Error{ErrorCode::InvalidArgument, 0};
    if (dir_path.empty() || !fs_is_dir(dir_path)) return Error{ErrorCode::NotFound, 0};

    ArenaTemp scratch = get_scratch(m_alloc);
    String norm = path_normalize(dir_path, m_alloc);
    for (usize i = 0; i < m_impl->entries.size(); ++i) {
        if (m_impl->entries[i]->dir.as_ref() == norm.as_ref()) {
            return Error::ok();
        }
    }
    if (m_impl->entries.size() >= MAXIMUM_WAIT_OBJECTS) {
        return Error{ErrorCode::OutOfRange, 0};
    }

    SmallArray<wchar_t, 260> wpath(scratch.allocator());
    if (!detail::utf8_path_to_win32_wide(norm.as_ref(), wpath)) {
        return Error{ErrorCode::InvalidArgument, 0};
    }

    HANDLE dh = CreateFileW(wpath.data(), FILE_LIST_DIRECTORY,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dh == INVALID_HANDLE_VALUE) {
        return detail::win32_error_to_keel(GetLastError());
    }
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ev) {
        const DWORD err = GetLastError();
        CloseHandle(dh);
        return detail::win32_error_to_keel(err);
    }

    void* emem = m_alloc->try_alloc_bytes(sizeof(Impl::Entry), alignof(Impl::Entry));
    if (!emem) {
        CloseHandle(ev);
        CloseHandle(dh);
        return Error{ErrorCode::OutOfRange, 0};
    }
    auto* entry = ::new (emem) Impl::Entry{};
    entry->dir_handle = dh;
    entry->ov_event = ev;
    entry->dir = keel::move(norm);
    entry->recursive = recursive;
    if (!m_impl->arm(entry)) {
        const DWORD err = GetLastError();
        m_impl->destroy_entry(entry);
        return detail::win32_error_to_keel(err);
    }
    m_impl->entries.push_back(entry);
    return Error::ok();
}

Error FileWatcher::unwatch_dir(StringRef dir_path) noexcept {
    if (!m_impl || dir_path.empty()) return Error{ErrorCode::InvalidArgument, 0};
    ArenaTemp scratch = get_scratch(m_alloc);
    const String norm = path_normalize(dir_path, scratch.allocator());
    for (usize i = 0; i < m_impl->entries.size(); ++i) {
        if (m_impl->entries[i]->dir.as_ref() == norm.as_ref()) {
            m_impl->destroy_entry(m_impl->entries[i]);
            m_impl->entries.erase(i);
            return Error::ok();
        }
    }
    return Error{ErrorCode::NotFound, 0};
}

Error FileWatcher::poll(u64 timeout_ms, FunctionRef<bool(const WatchEvent&)> callback) {
    if (!m_impl || !callback) return Error{ErrorCode::InvalidArgument, 0};
    if (m_impl->entries.empty()) return Error::ok();

    HANDLE handles[MAXIMUM_WAIT_OBJECTS]{};
    const DWORD count = as<DWORD>(m_impl->entries.size());
    for (DWORD i = 0; i < count; ++i) {
        handles[i] = m_impl->entries[i]->ov_event;
    }
    const DWORD wait_ms = (timeout_ms >= 0xFFFFFFFEULL) ? 0xFFFFFFFEu : as<DWORD>(timeout_ms);
    const DWORD wr = WaitForMultipleObjects(count, handles, FALSE, wait_ms);
    if (wr == WAIT_TIMEOUT) return Error::ok();
    if (wr == WAIT_FAILED) return detail::win32_error_to_keel(GetLastError());

    bool keep_going = true;
    usize i = 0;
    while (i < m_impl->entries.size() && keep_going) {
        auto* entry = m_impl->entries[i];
        if (WaitForSingleObject(entry->ov_event, 0) != WAIT_OBJECT_0) {
            ++i;
            continue;
        }

        DWORD bytes = 0;
        const BOOL ok = GetOverlappedResult(entry->dir_handle, &entry->ov, &bytes, FALSE);
        entry->pending = false;
        ResetEvent(entry->ov_event);
        if (ok && bytes > 0) {
            usize off = 0;
            while (off < as<usize>(bytes) && keep_going) {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(entry->buf + off);
                ArenaTemp scratch = get_scratch(m_alloc);
                String rel(scratch.allocator());
                detail::win32_wide_to_utf8_path(info->FileName, as<usize>(info->FileNameLength) / sizeof(wchar_t), rel);
                const String full = path_join(entry->dir.as_ref(), rel.as_ref(), scratch.allocator());

                WatchEventKind kind = WatchEventKind::Modified;
                switch (info->Action) {
                case FILE_ACTION_ADDED:
                    kind = WatchEventKind::Added;
                    break;
                case FILE_ACTION_REMOVED:
                    kind = WatchEventKind::Removed;
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                case FILE_ACTION_RENAMED_NEW_NAME:
                    kind = WatchEventKind::Renamed;
                    break;
                default:
                    kind = WatchEventKind::Modified;
                    break;
                }
                const WatchEvent we{kind, full.as_ref()};
                if (!callback(we)) {
                    keep_going = false;
                }
                if (info->NextEntryOffset == 0) break;
                off += as<usize>(info->NextEntryOffset);
            }
        }
        // If re-arming fails (e.g., watched directory was deleted), remove the dead entry to prevent 100% CPU spin.
        if (!m_impl->arm(entry)) {
            m_impl->destroy_entry(entry);
            m_impl->entries.erase(i);
            continue;
        }
        ++i;
    }
    return Error::ok();
}

void FileWatcher::close() noexcept {
    if (m_impl && m_alloc) {
        m_impl->~Impl();
        m_alloc->free_bytes(m_impl, sizeof(Impl), alignof(Impl));
    }
    m_impl = nullptr;
    m_alloc = nullptr;
}

#else // KEEL_FSWATCH_BACKEND_KQUEUE (macOS / BSD)

struct FileWatcher::Impl {
    static constexpr usize kMaxFileVnodes = 512u;

    struct FileSnap {
        String name;
        u64 size = 0;
        i64 mtime_ns = 0;
        bool is_dir = false;
    };

    struct DirEntryWatch {
        int fd = -1;
        String dir;
        bool recursive = false;
        Array<FileSnap> snaps;
    };

    struct FileVnodeWatch {
        int fd = -1;
        String path;
    };

    int kq = -1;
    Allocator* alloc = nullptr;
    Array<DirEntryWatch> dirs;
    Array<FileVnodeWatch> files;

    explicit Impl(int kq_fd, Allocator* a) : kq(kq_fd), alloc(a), dirs(a), files(a) {}

    ~Impl() noexcept {
        for (usize i = 0; i < files.size(); ++i) {
            if (files[i].fd >= 0) (void)::close(files[i].fd);
        }
        for (usize i = 0; i < dirs.size(); ++i) {
            if (dirs[i].fd >= 0) (void)::close(dirs[i].fd);
        }
        if (kq >= 0) {
            (void)::close(kq);
            kq = -1;
        }
    }

    static int open_vnode_fd(const char* cpath) noexcept {
#if defined(O_EVTONLY)
        return ::open(cpath, O_EVTONLY | O_CLOEXEC);
#else
        return ::open(cpath, O_RDONLY | O_CLOEXEC);
#endif
    }

    void watch_file_vnode(StringRef full_path) {
        if (files.size() >= kMaxFileVnodes) return;
        for (usize i = 0; i < files.size(); ++i) {
            if (files[i].path.as_ref() == full_path) return;
        }
        String cpath = String::from(full_path, alloc);
        const int fd = open_vnode_fd(cpath.c_str());
        if (fd < 0) return;
        struct kevent kev{};
        EV_SET(&kev, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
               NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME, 0, reinterpret_cast<void*>(1));
        if (::kevent(kq, &kev, 1, nullptr, 0, nullptr) < 0) {
            (void)::close(fd);
            return;
        }
        files.push_back(FileVnodeWatch{fd, keel::move(cpath)});
    }

    void unwatch_file_vnode(StringRef full_path) noexcept {
        for (usize i = 0; i < files.size(); ++i) {
            if (files[i].path.as_ref() == full_path) {
                if (files[i].fd >= 0) (void)::close(files[i].fd);
                files.erase(i);
                return;
            }
        }
    }

    Array<FileSnap> snapshot_dir(StringRef dir, bool register_file_vnodes) {
        Array<FileSnap> out(alloc);
        (void)fs_read_dir(dir, [&](const DirEntry& ent) {
            FileSnap s{String::from(ent.name, alloc), 0, 0, ent.is_dir};
            if (auto st = fs_stat(ent.full_path); st.is_ok()) {
                s.size = st->size;
                s.mtime_ns = st->mtime_ns;
            }
            if (register_file_vnodes && ent.is_file) {
                watch_file_vnode(ent.full_path);
            }
            out.push_back(keel::move(s));
            return true;
        });
        return out;
    }

    Error add_single(StringRef norm_dir, bool recursive) {
        for (usize i = 0; i < dirs.size(); ++i) {
            if (dirs[i].dir.as_ref() == norm_dir) {
                dirs[i].recursive = dirs[i].recursive || recursive;
                return Error::ok();
            }
        }
        String cdir = String::from(norm_dir, alloc);
        const int fd = open_vnode_fd(cdir.c_str());
        if (fd < 0) return detail::errno_to_keel(errno);

        struct kevent kev{};
        EV_SET(&kev, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
               NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME, 0, nullptr);
        if (::kevent(kq, &kev, 1, nullptr, 0, nullptr) < 0) {
            const int err = errno;
            (void)::close(fd);
            return detail::errno_to_keel(err);
        }
        Array<FileSnap> snap = snapshot_dir(cdir.as_ref(), true);
        dirs.push_back(DirEntryWatch{fd, keel::move(cdir), recursive, keel::move(snap)});
        return Error::ok();
    }
};

Result<FileWatcher> FileWatcher::create(Allocator* alloc) {
    Allocator* a = alloc ? alloc : default_allocator();
    const int kq = ::kqueue();
    if (kq < 0) return detail::errno_to_keel(errno);
    (void)::fcntl(kq, F_SETFD, FD_CLOEXEC);
    void* mem = a->try_alloc_bytes(sizeof(Impl), alignof(Impl));
    if (!mem) {
        (void)::close(kq);
        return Error{ErrorCode::OutOfRange, 0};
    }
    FileWatcher out;
    out.m_impl = ::new (mem) Impl(kq, a);
    out.m_alloc = a;
    return out;
}

Error FileWatcher::watch_dir(StringRef dir_path, bool recursive) {
    if (!m_impl) return Error{ErrorCode::InvalidArgument, 0};
    if (dir_path.empty() || !fs_is_dir(dir_path)) return Error{ErrorCode::NotFound, 0};

    ArenaTemp scratch = get_scratch(m_alloc);
    String norm = path_normalize(dir_path, scratch.allocator());
    const Error root_err = m_impl->add_single(norm.as_ref(), recursive);
    if (root_err.is_err()) return root_err;

    if (recursive) {
        (void)fs_walk_dir(norm.as_ref(), [&](const DirEntry& ent) {
            if (ent.is_dir) {
                String sub = path_normalize(ent.full_path, scratch.allocator());
                (void)m_impl->add_single(sub.as_ref(), true);
            }
            return true;
        });
    }
    return Error::ok();
}

Error FileWatcher::unwatch_dir(StringRef dir_path) noexcept {
    if (!m_impl || dir_path.empty()) return Error{ErrorCode::InvalidArgument, 0};
    ArenaTemp scratch = get_scratch(m_alloc);
    const String norm = path_normalize(dir_path, scratch.allocator());
    auto matches_subtree = [&](StringRef cur) noexcept -> bool {
        if (cur == norm.as_ref()) return true;
        if (norm.as_ref() == "/") return cur.starts_with('/');
        return cur.size() > norm.size() && cur.starts_with(norm.as_ref()) && cur[norm.size()] == '/';
    };

    bool found = false;
    usize i = 0;
    while (i < m_impl->dirs.size()) {
        if (matches_subtree(m_impl->dirs[i].dir.as_ref())) {
            if (m_impl->dirs[i].fd >= 0) (void)::close(m_impl->dirs[i].fd);
            m_impl->dirs.erase(i);
            found = true;
        } else {
            ++i;
        }
    }
    usize f = 0;
    while (f < m_impl->files.size()) {
        if (matches_subtree(m_impl->files[f].path.as_ref())) {
            if (m_impl->files[f].fd >= 0) (void)::close(m_impl->files[f].fd);
            m_impl->files.erase(f);
        } else {
            ++f;
        }
    }
    return found ? Error::ok() : Error{ErrorCode::NotFound, 0};
}

Error FileWatcher::poll(u64 timeout_ms, FunctionRef<bool(const WatchEvent&)> callback) {
    if (!m_impl || !callback) return Error{ErrorCode::InvalidArgument, 0};

    struct timespec ts{};
    ts.tv_sec = static_cast<time_t>(timeout_ms / 1000ULL);
    ts.tv_nsec = static_cast<long>((timeout_ms % 1000ULL) * 1000000ULL);

    struct kevent events[32]{};
    int nev = 0;
    do {
        nev = ::kevent(m_impl->kq, nullptr, 0, events, 32, &ts);
    } while (nev < 0 && errno == EINTR);
    if (nev < 0) return detail::errno_to_keel(errno);
    if (nev == 0) return Error::ok();

    bool keep_going = true;
    for (int k = 0; k < nev && keep_going; ++k) {
        const int fd = static_cast<int>(events[k].ident);
        const bool is_file_event = (events[k].udata != nullptr);
        if (is_file_event) {
            usize fidx = kInvalidIndex;
            for (usize i = 0; i < m_impl->files.size(); ++i) {
                if (m_impl->files[i].fd == fd) {
                    fidx = i;
                    break;
                }
            }
            if (fidx == kInvalidIndex) continue;
            ArenaTemp scratch = get_scratch(m_alloc);
            const String fpath = String::from(m_impl->files[fidx].path.as_ref(), scratch.allocator());
            WatchEventKind kind = WatchEventKind::Modified;
            if ((events[k].fflags & NOTE_DELETE) != 0) {
                kind = WatchEventKind::Removed;
                (void)::close(m_impl->files[fidx].fd);
                m_impl->files.erase(fidx);
            } else if ((events[k].fflags & NOTE_RENAME) != 0) {
                kind = WatchEventKind::Renamed;
                (void)::close(m_impl->files[fidx].fd);
                m_impl->files.erase(fidx);
            }
            const WatchEvent we{kind, fpath.as_ref()};
            if (!callback(we)) keep_going = false;
            continue;
        }

        usize idx = kInvalidIndex;
        for (usize i = 0; i < m_impl->dirs.size(); ++i) {
            if (m_impl->dirs[i].fd == fd) {
                idx = i;
                break;
            }
        }
        if (idx == kInvalidIndex) continue;

        Array<Impl::FileSnap> next = m_impl->snapshot_dir(m_impl->dirs[idx].dir.as_ref(), false);
        const Array<Impl::FileSnap>& prev = m_impl->dirs[idx].snaps;
        ArenaTemp scratch = get_scratch(m_alloc);

        // Added or Modified
        for (usize i = 0; i < next.size() && keep_going; ++i) {
            bool existed = false;
            bool changed = false;
            for (usize j = 0; j < prev.size(); ++j) {
                if (prev[j].name.as_ref() == next[i].name.as_ref()) {
                    existed = true;
                    changed = (prev[j].size != next[i].size || prev[j].mtime_ns != next[i].mtime_ns);
                    break;
                }
            }
            if (!existed || changed) {
                const String full =
                    path_join(m_impl->dirs[idx].dir.as_ref(), next[i].name.as_ref(), scratch.allocator());
                if (!existed) {
                    if (next[i].is_dir && m_impl->dirs[idx].recursive) {
                        (void)m_impl->add_single(full.as_ref(), true);
                    } else if (!next[i].is_dir) {
                        m_impl->watch_file_vnode(full.as_ref());
                    }
                }
                const WatchEvent we{existed ? WatchEventKind::Modified : WatchEventKind::Added, full.as_ref()};
                if (!callback(we)) keep_going = false;
            }
        }
        // Removed
        for (usize j = 0; j < prev.size() && keep_going; ++j) {
            bool still_there = false;
            for (usize i = 0; i < next.size(); ++i) {
                if (next[i].name.as_ref() == prev[j].name.as_ref()) {
                    still_there = true;
                    break;
                }
            }
            if (!still_there) {
                const String full =
                    path_join(m_impl->dirs[idx].dir.as_ref(), prev[j].name.as_ref(), scratch.allocator());
                m_impl->unwatch_file_vnode(full.as_ref());
                const WatchEvent we{WatchEventKind::Removed, full.as_ref()};
                if (!callback(we)) keep_going = false;
            }
        }
        m_impl->dirs[idx].snaps = keel::move(next);
    }
    return Error::ok();
}

void FileWatcher::close() noexcept {
    if (m_impl && m_alloc) {
        m_impl->~Impl();
        m_alloc->free_bytes(m_impl, sizeof(Impl), alignof(Impl));
    }
    m_impl = nullptr;
    m_alloc = nullptr;
}

#endif

} // namespace keel
