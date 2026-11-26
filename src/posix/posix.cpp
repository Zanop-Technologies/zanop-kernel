// POSIX layer: portable core (fd tables, path handling, syscall semantics).
// Talks to the kernel only through posix::Backend, so it is unit-tested on a
// host (tests/posix). See docs/posix.md for the design and its limits.
#include "posix.hpp"

namespace posix {
namespace {

const Backend* be = nullptr;

constexpr usize MAX_PROCS = 16;
constexpr usize MAX_FDS = 32;
constexpr usize MAX_OFDS = 64;
constexpr usize MAX_MAPS = 32;
constexpr usize PAGE = 4096;
constexpr u32 FLAGS_KEPT = oflag::ACCMODE | oflag::APPEND | oflag::NONBLOCK;

enum class Kind : u8 { Free, ConsoleIn, ConsoleOut, File, Dir };

// An open file description: shared by dup()'d descriptors.
struct Ofd {
    Kind kind;
    u32 refs;
    u32 flags;
    i64 offset; // byte offset for files, entry index for directories
    char path[PATH_LIMIT];
};

struct Fd {
    i16 ofd; // -1 when the descriptor is closed
    u8 cloexec;
};

struct Proc {
    bool used;
    u32 pid;
    char cwd[PATH_LIMIT];
    Fd fds[MAX_FDS];
};

struct Map {
    void* addr;
    usize len;
};

Ofd ofds[MAX_OFDS];
Proc procs[MAX_PROCS];
Map maps[MAX_MAPS];
u32 umask_value = 022;

// While a ring-3 program runs, pointers it passes are checked against its
// memory region and its descriptors live under a virtual process id.
bool in_user = false;
u32 user_pid = 0;
u64 user_lo = 0;
u64 user_hi = 0;

// ---- small helpers (no libc in the kernel) --------------------------------

usize str_len(const char* s) {
    usize n = 0;
    while (s[n]) ++n;
    return n;
}

void str_copy(char* out, usize cap, const char* in) {
    usize i = 0;
    if (cap == 0) return;
    while (i + 1 < cap && in[i]) {
        out[i] = in[i];
        ++i;
    }
    out[i] = '\0';
}

void zero(void* p, usize n) {
    u8* b = static_cast<u8*>(p);
    for (usize i = 0; i < n; ++i) b[i] = 0;
}

// Validates a pointer handed in by a caller. Kernel callers only need to be
// non-null; a ring-3 program may only reach its own region, so a pointer into
// kernel memory is refused instead of being dereferenced on its behalf.
bool user_ok(const void* p, usize n) {
    if (p == nullptr) return false;
    if (!in_user) return true;
    const u64 a = reinterpret_cast<u64>(p);
    if (a < user_lo || a >= user_hi) return false;
    return n <= user_hi - a;
}

// Like user_ok for a NUL-terminated string: the terminator must lie inside the
// region too, or reading it would run past the end.
bool user_cstr_ok(const char* s) {
    if (s == nullptr) return false;
    if (!in_user) return true;
    const u64 a = reinterpret_cast<u64>(s);
    if (a < user_lo || a >= user_hi) return false;
    for (u64 i = a; i < user_hi; ++i)
        if (*reinterpret_cast<const char*>(i) == '\0') return true;
    return false;
}

u32 current_pid() { return in_user ? user_pid : be->cur_pid(); }
bool pid_is_alive(u32 pid) { return (in_user && pid == user_pid) || be->pid_alive(pid); }

void put_u16(u8* d, u16 v) { d[0] = static_cast<u8>(v); d[1] = static_cast<u8>(v >> 8); }
void put_u64(u8* d, u64 v) { for (int i = 0; i < 8; ++i) d[i] = static_cast<u8>(v >> (8 * i)); }

// ---- paths ----------------------------------------------------------------

// Appends the components of `s` to the canonical path in out/len.
i64 apply(char* out, usize& len, const char* s) {
    usize i = 0;
    while (s[i]) {
        while (s[i] == '/') ++i;
        const usize start = i;
        while (s[i] && s[i] != '/') ++i;
        const usize seg = i - start;
        if (seg == 0 || (seg == 1 && s[start] == '.')) continue;
        if (seg == 2 && s[start] == '.' && s[start + 1] == '.') {
            while (len > 1 && out[len - 1] != '/') --len;
            if (len > 1) --len;
            out[len] = '\0';
            continue;
        }
        if (seg > NAME_LIMIT) return -err::NAMETOOLONG;
        const usize need = (len > 1 ? 1 : 0) + seg;
        if (len + need >= PATH_LIMIT) return -err::NAMETOOLONG;
        if (len > 1) out[len++] = '/';
        for (usize k = 0; k < seg; ++k) out[len++] = s[start + k];
        out[len] = '\0';
    }
    return 0;
}

i64 resolve(const Proc& p, const char* path, char* out) {
    if (!user_cstr_ok(path)) return -err::FAULT;
    if (path[0] == '\0') return -err::NOENT;
    usize len = 1;
    out[0] = '/';
    out[1] = '\0';
    if (path[0] != '/') {
        str_copy(out, PATH_LIMIT, p.cwd);
        len = str_len(out);
    }
    return apply(out, len, path);
}

void parent_of(const char* path, char* out) {
    usize len = str_len(path);
    while (len > 1 && path[len - 1] != '/') --len;
    if (len > 1) --len;
    for (usize i = 0; i < len; ++i) out[i] = path[i];
    out[len] = '\0';
}

// The parent of `path` must exist and be a directory.
i64 check_parent(const char* path) {
    char parent[PATH_LIMIT];
    parent_of(path, parent);
    FsStat st;
    const i64 r = be->fs_stat(parent, &st);
    if (r < 0) return r;
    return st.is_dir ? 0 : -err::NOTDIR;
}

// ---- processes and descriptors -------------------------------------------

void release_ofd(int index) {
    Ofd& o = ofds[index];
    if (o.refs > 0) --o.refs;
    if (o.refs == 0 && index >= 3) zero(&o, sizeof(o));
}

void close_slot(Proc& p, usize fd) {
    if (p.fds[fd].ofd < 0) return;
    release_ofd(p.fds[fd].ofd);
    p.fds[fd].ofd = -1;
    p.fds[fd].cloexec = 0;
}

void drop(Proc& p) {
    for (usize fd = 0; fd < MAX_FDS; ++fd) close_slot(p, fd);
    p.used = false;
}

Proc* proc_for(u32 pid) {
    for (Proc& p : procs)
        if (p.used && p.pid == pid) return &p;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            // Table full: reclaim processes that have gone away.
            for (Proc& p : procs)
                if (p.used && !pid_is_alive(p.pid)) drop(p);
        }
        for (Proc& p : procs) {
            if (p.used) continue;
            zero(&p, sizeof(p));
            p.used = true;
            p.pid = pid;
            p.cwd[0] = '/';
            for (usize fd = 0; fd < MAX_FDS; ++fd) p.fds[fd].ofd = -1;
            for (int fd = 0; fd < 3; ++fd) {
                p.fds[fd].ofd = static_cast<i16>(fd);
                ++ofds[fd].refs;
            }
            return &p;
        }
    }
    return nullptr;
}

i64 alloc_fd(Proc& p, usize min) {
    for (usize fd = min; fd < MAX_FDS; ++fd)
        if (p.fds[fd].ofd < 0) return static_cast<i64>(fd);
    return -err::MFILE;
}

i64 alloc_ofd() {
    for (usize i = 3; i < MAX_OFDS; ++i)
        if (ofds[i].kind == Kind::Free) return static_cast<i64>(i);
    return -err::NFILE;
}

Ofd* get(Proc& p, i64 fd) {
    if (fd < 0 || fd >= static_cast<i64>(MAX_FDS) || p.fds[fd].ofd < 0) return nullptr;
    return &ofds[p.fds[fd].ofd];
}

bool is_console(const Ofd& o) { return o.kind == Kind::ConsoleIn || o.kind == Kind::ConsoleOut; }

// ---- stat -----------------------------------------------------------------

void fill_stat(Stat* st, bool is_dir, u64 size, u64 ino) {
    zero(st, sizeof(*st));
    st->st_ino = ino;
    st->st_nlink = is_dir ? 2 : 1;
    st->st_mode = is_dir ? (mode::IFDIR | 0755) : (mode::IFREG | 0644);
    st->st_size = static_cast<i64>(size);
    st->st_blksize = 512;
    st->st_blocks = static_cast<i64>((size + 511) / 512);
}

i64 stat_path(const Proc& p, const char* path, Stat* out) {
    if (!user_ok(out, sizeof(Stat))) return -err::FAULT;
    char abs[PATH_LIMIT];
    i64 r = resolve(p, path, abs);
    if (r < 0) return r;
    FsStat fs;
    r = be->fs_stat(abs, &fs);
    if (r < 0) return r;
    u64 ino = 1;
    for (usize i = 0; abs[i]; ++i) ino = ino * 131 + static_cast<u8>(abs[i]);
    fill_stat(out, fs.is_dir, fs.size, ino);
    return 0;
}

i64 sys_fstat(Proc& p, i64 fd, Stat* out) {
    if (!user_ok(out, sizeof(Stat))) return -err::FAULT;
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    if (is_console(*o)) {
        fill_stat(out, false, 0, 100 + static_cast<u64>(o->kind));
        out->st_mode = mode::IFCHR | 0620;
        return 0;
    }
    FsStat fs;
    const i64 r = be->fs_stat(o->path, &fs);
    if (r < 0) return r;
    fill_stat(out, fs.is_dir, fs.size, 1);
    return 0;
}

// ---- open / close ----------------------------------------------------------

i64 sys_open(Proc& p, const char* path, u64 flags64) {
    const u32 flags = static_cast<u32>(flags64);
    const u32 acc = flags & oflag::ACCMODE;
    if (acc == 3) return -err::INVAL;
    char abs[PATH_LIMIT];
    i64 r = resolve(p, path, abs);
    if (r < 0) return r;

    FsStat st = {false, 0};
    r = be->fs_stat(abs, &st);
    if (r < 0 && r != -err::NOENT) return r;
    if (r == -err::NOENT) {
        if (!(flags & oflag::CREAT)) return -err::NOENT;
        r = check_parent(abs);
        if (r < 0) return r;
        r = be->fs_create(abs);
        if (r < 0) return r;
        st = {false, 0};
    } else {
        if ((flags & oflag::CREAT) && (flags & oflag::EXCL)) return -err::EXIST;
        if (st.is_dir) {
            if (acc != oflag::RDONLY) return -err::ISDIR;
        } else {
            if (flags & oflag::DIRECTORY) return -err::NOTDIR;
            if ((flags & oflag::TRUNC) && acc != oflag::RDONLY) {
                r = be->fs_truncate(abs, 0);
                if (r < 0) return r;
            }
        }
    }

    const i64 fd = alloc_fd(p, 0);
    if (fd < 0) return fd;
    const i64 oi = alloc_ofd();
    if (oi < 0) return oi;
    Ofd& o = ofds[oi];
    zero(&o, sizeof(o));
    o.kind = st.is_dir ? Kind::Dir : Kind::File;
    o.refs = 1;
    o.flags = flags & FLAGS_KEPT;
    str_copy(o.path, PATH_LIMIT, abs);
    p.fds[fd].ofd = static_cast<i16>(oi);
    p.fds[fd].cloexec = (flags & oflag::CLOEXEC) ? 1 : 0;
    return fd;
}

i64 sys_close(Proc& p, i64 fd) {
    if (!get(p, fd)) return -err::BADF;
    close_slot(p, static_cast<usize>(fd));
    return 0;
}

// ---- read / write ----------------------------------------------------------

i64 do_read(Ofd& o, void* buf, usize n, i64* offset) {
    if (n == 0) return 0;
    if (!user_ok(buf, n)) return -err::FAULT;
    switch (o.kind) {
    case Kind::ConsoleIn: return be->con_read(buf, n);
    case Kind::File: {
        if ((o.flags & oflag::ACCMODE) == oflag::WRONLY) return -err::BADF;
        const i64 r = be->fs_read(o.path, static_cast<u64>(*offset), buf, n);
        if (r > 0) *offset += r;
        return r;
    }
    case Kind::Dir: return -err::ISDIR;
    default: return -err::BADF;
    }
}

i64 do_write(Ofd& o, const void* buf, usize n, i64* offset, bool positional) {
    if (n == 0) return 0;
    if (!user_ok(buf, n)) return -err::FAULT;
    switch (o.kind) {
    case Kind::ConsoleOut: return be->con_write(buf, n);
    case Kind::File: {
        if ((o.flags & oflag::ACCMODE) == oflag::RDONLY) return -err::BADF;
        if ((o.flags & oflag::APPEND) && !positional) {
            FsStat st;
            const i64 s = be->fs_stat(o.path, &st);
            if (s < 0) return s;
            *offset = static_cast<i64>(st.size);
        }
        const i64 r = be->fs_write(o.path, static_cast<u64>(*offset), buf, n);
        if (r > 0) *offset += r;
        return r;
    }
    default: return -err::BADF;
    }
}

i64 sys_read(Proc& p, i64 fd, void* buf, usize n) {
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    return do_read(*o, buf, n, &o->offset);
}

i64 sys_write(Proc& p, i64 fd, const void* buf, usize n) {
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    return do_write(*o, buf, n, &o->offset, false);
}

i64 sys_pread(Proc& p, i64 fd, void* buf, usize n, i64 off, bool write) {
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    if (o->kind != Kind::File) return o->kind == Kind::Dir ? -err::ISDIR : -err::SPIPE;
    if (off < 0) return -err::INVAL;
    i64 pos = off; // local copy: positional I/O leaves the shared offset alone
    return write ? do_write(*o, buf, n, &pos, true) : do_read(*o, buf, n, &pos);
}

i64 sys_vector(Proc& p, i64 fd, const IoVec* iov, i64 count, bool write) {
    if (count < 0 || count > 1024) return -err::INVAL;
    if (count && !user_ok(iov, sizeof(IoVec) * static_cast<usize>(count))) return -err::FAULT;
    i64 total = 0;
    for (i64 i = 0; i < count; ++i) {
        if (iov[i].iov_len == 0) continue;
        const i64 r = write ? sys_write(p, fd, iov[i].iov_base, iov[i].iov_len)
                            : sys_read(p, fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total > 0 ? total : r;
        total += r;
        if (static_cast<usize>(r) < iov[i].iov_len) break;
    }
    return total;
}

i64 sys_lseek(Proc& p, i64 fd, i64 off, i64 whence) {
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    if (is_console(*o)) return -err::SPIPE;
    i64 base = 0;
    if (whence == seek::CUR) base = o->offset;
    else if (whence == seek::END) {
        FsStat st;
        const i64 r = be->fs_stat(o->path, &st);
        if (r < 0) return r;
        base = static_cast<i64>(st.size);
    } else if (whence != seek::SET) return -err::INVAL;
    const i64 target = base + off;
    if (target < 0) return -err::INVAL;
    if (o->kind == Kind::Dir && target != 0) return -err::INVAL; // only rewind
    o->offset = target;
    return target;
}

i64 sys_getdents(Proc& p, i64 fd, u8* buf, usize count) {
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    if (o->kind != Kind::Dir) return -err::NOTDIR;
    if (!user_ok(buf, count)) return -err::FAULT;
    usize written = 0;
    for (;;) {
        DirEntry e;
        const i64 pos = o->offset;
        if (pos == 0 || pos == 1) {
            e.name[0] = '.';
            e.name[1] = pos == 1 ? '.' : '\0';
            e.name[2] = '\0';
            e.is_dir = true;
        } else {
            const i64 r = be->fs_readdir(o->path, static_cast<usize>(pos - 2), &e);
            if (r < 0) return written ? static_cast<i64>(written) : r;
            if (r == 0) break;
        }
        const usize nlen = str_len(e.name);
        const usize reclen = (DIRENT64_HEADER + nlen + 1 + 7) & ~static_cast<usize>(7);
        if (written + reclen > count) {
            if (written == 0) return -err::INVAL;
            break;
        }
        u8* d = buf + written;
        zero(d, reclen);
        put_u64(d, static_cast<u64>(pos) + 1);
        put_u64(d + 8, static_cast<u64>(pos) + 1);
        put_u16(d + 16, static_cast<u16>(reclen));
        d[18] = e.is_dir ? dtype::DIR : dtype::REG;
        for (usize i = 0; i < nlen; ++i) d[DIRENT64_HEADER + i] = static_cast<u8>(e.name[i]);
        written += reclen;
        ++o->offset;
    }
    return static_cast<i64>(written);
}

// ---- namespace operations --------------------------------------------------

i64 sys_mkdir(Proc& p, const char* path) {
    char abs[PATH_LIMIT];
    i64 r = resolve(p, path, abs);
    if (r < 0) return r;
    FsStat st;
    r = be->fs_stat(abs, &st);
    if (r == 0) return -err::EXIST;
    if (r != -err::NOENT) return r;
    r = check_parent(abs);
    if (r < 0) return r;
    return be->fs_mkdir(abs);
}

i64 sys_rmdir(Proc& p, const char* path) {
    char abs[PATH_LIMIT];
    i64 r = resolve(p, path, abs);
    if (r < 0) return r;
    if (abs[1] == '\0') return -err::BUSY;
    FsStat st;
    r = be->fs_stat(abs, &st);
    if (r < 0) return r;
    if (!st.is_dir) return -err::NOTDIR;
    DirEntry e;
    if (be->fs_readdir(abs, 0, &e) == 1) return -err::NOTEMPTY;
    return be->fs_rmdir(abs);
}

i64 sys_unlink(Proc& p, const char* path) {
    char abs[PATH_LIMIT];
    i64 r = resolve(p, path, abs);
    if (r < 0) return r;
    FsStat st;
    r = be->fs_stat(abs, &st);
    if (r < 0) return r;
    if (st.is_dir) return -err::ISDIR;
    return be->fs_unlink(abs);
}

i64 sys_rename(Proc& p, const char* from, const char* to) {
    char a[PATH_LIMIT], b[PATH_LIMIT];
    i64 r = resolve(p, from, a);
    if (r < 0) return r;
    r = resolve(p, to, b);
    if (r < 0) return r;
    FsStat sa, sb;
    r = be->fs_stat(a, &sa);
    if (r < 0) return r;
    if (sa.is_dir) return -err::NOSYS; // directory rename not supported yet
    if (!be->fs_rename) return -err::NOSYS;
    r = be->fs_stat(b, &sb);
    if (r == 0) {
        if (sb.is_dir) return -err::ISDIR;
        bool same = true;
        for (usize i = 0; i < PATH_LIMIT && same; ++i) {
            if (a[i] != b[i]) same = false;
            if (!a[i]) break;
        }
        if (same) return 0;
    } else if (r != -err::NOENT) {
        return r;
    }
    r = check_parent(b);
    if (r < 0) return r;
    return be->fs_rename(a, b);
}

i64 sys_access(const Proc& p, const char* path) {
    char abs[PATH_LIMIT];
    i64 r = resolve(p, path, abs);
    if (r < 0) return r;
    FsStat st;
    r = be->fs_stat(abs, &st);
    return r < 0 ? r : 0;
}

i64 sys_chdir(Proc& p, const char* path) {
    char abs[PATH_LIMIT];
    i64 r = resolve(p, path, abs);
    if (r < 0) return r;
    FsStat st;
    r = be->fs_stat(abs, &st);
    if (r < 0) return r;
    if (!st.is_dir) return -err::NOTDIR;
    str_copy(p.cwd, PATH_LIMIT, abs);
    return 0;
}

i64 sys_getcwd(const Proc& p, char* buf, usize size) {
    const usize need = str_len(p.cwd) + 1;
    if (!user_ok(buf, size)) return -err::FAULT;
    if (size < need) return -err::RANGE;
    for (usize i = 0; i < need; ++i) buf[i] = p.cwd[i];
    return static_cast<i64>(need);
}

i64 sys_ftruncate(Proc& p, i64 fd, i64 length) {
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    if (o->kind != Kind::File || (o->flags & oflag::ACCMODE) == oflag::RDONLY || length < 0)
        return -err::INVAL;
    return be->fs_truncate(o->path, static_cast<u64>(length));
}

// ---- descriptors: dup / fcntl / ioctl --------------------------------------

i64 dup_to(Proc& p, i64 fd, usize min, bool cloexec) {
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    if (min >= MAX_FDS) return -err::INVAL;
    const i64 nfd = alloc_fd(p, min);
    if (nfd < 0) return nfd;
    p.fds[nfd].ofd = p.fds[fd].ofd;
    p.fds[nfd].cloexec = cloexec ? 1 : 0;
    ++o->refs;
    return nfd;
}

i64 sys_dup2(Proc& p, i64 oldfd, i64 newfd) {
    Ofd* o = get(p, oldfd);
    if (!o) return -err::BADF;
    if (newfd < 0 || newfd >= static_cast<i64>(MAX_FDS)) return -err::BADF;
    if (oldfd == newfd) return newfd;
    close_slot(p, static_cast<usize>(newfd));
    p.fds[newfd].ofd = p.fds[oldfd].ofd;
    p.fds[newfd].cloexec = 0;
    ++o->refs;
    return newfd;
}

i64 sys_fcntl(Proc& p, i64 fd, i64 cmd, u64 arg) {
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    switch (cmd) {
    case fcmd::DUPFD: return dup_to(p, fd, static_cast<usize>(arg), false);
    case fcmd::DUPFD_CLOEXEC: return dup_to(p, fd, static_cast<usize>(arg), true);
    case fcmd::GETFD: return p.fds[fd].cloexec ? fcmd::FD_CLOEXEC : 0;
    case fcmd::SETFD: p.fds[fd].cloexec = (arg & fcmd::FD_CLOEXEC) ? 1 : 0; return 0;
    case fcmd::GETFL: return static_cast<i64>(o->flags);
    case fcmd::SETFL: {
        const u32 changeable = oflag::APPEND | oflag::NONBLOCK;
        o->flags = (o->flags & ~changeable) | (static_cast<u32>(arg) & changeable);
        return 0;
    }
    default: return -err::INVAL;
    }
}

i64 sys_ioctl(Proc& p, i64 fd, u64 req, u64 arg) {
    Ofd* o = get(p, fd);
    if (!o) return -err::BADF;
    if (!is_console(*o)) return -err::NOTTY;
    if (req == ioc::TCGETS) {
        if (arg && !user_ok(reinterpret_cast<void*>(arg), 36)) return -err::FAULT;
        if (arg) zero(reinterpret_cast<void*>(arg), 36); // struct termios
        return 0;
    }
    if (req == ioc::TIOCGWINSZ) {
        if (!user_ok(reinterpret_cast<void*>(arg), sizeof(WinSize))) return -err::FAULT;
        WinSize* ws = reinterpret_cast<WinSize*>(arg);
        ws->ws_row = 25;
        ws->ws_col = 80;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        return 0;
    }
    return -err::INVAL;
}

// ---- memory ----------------------------------------------------------------

i64 sys_mmap(u64 len, u64 flags, i64 fd) {
    if (len == 0 || len > 0x7FFFFFFF) return -err::INVAL;
    // A ring-3 program has one fixed region and no per-process page tables to
    // map more memory into, so mapping from it waits for those.
    if (in_user) return -err::NOSYS;
    if (!(flags & mmapf::ANONYMOUS)) return -err::NODEV; // file mappings: not yet
    if (!(flags & mmapf::PRIVATE) || (flags & 0x10 /* MAP_FIXED */)) return -err::INVAL;
    if (fd != -1) return -err::INVAL;
    const usize size = (static_cast<usize>(len) + PAGE - 1) & ~(PAGE - 1);
    usize slot = MAX_MAPS;
    for (usize i = 0; i < MAX_MAPS; ++i)
        if (!maps[i].addr) { slot = i; break; }
    if (slot == MAX_MAPS) return -err::NOMEM;
    void* mem = be->mem_alloc(size, PAGE);
    if (!mem) return -err::NOMEM;
    zero(mem, size);
    maps[slot].addr = mem;
    maps[slot].len = size;
    return static_cast<i64>(reinterpret_cast<u64>(mem));
}

i64 sys_munmap(u64 addr, u64 len) {
    const usize size = (static_cast<usize>(len) + PAGE - 1) & ~(PAGE - 1);
    for (Map& m : maps) {
        if (m.addr && reinterpret_cast<u64>(m.addr) == addr) {
            if (m.len != size) return -err::INVAL; // partial unmap: not supported
            be->mem_free(m.addr);
            m.addr = nullptr;
            m.len = 0;
            return 0;
        }
    }
    return -err::INVAL;
}

// ---- misc -------------------------------------------------------------------

i64 sys_uname(UtsName* u) {
    if (!user_ok(u, sizeof(UtsName))) return -err::FAULT;
    zero(u, sizeof(*u));
    str_copy(u->sysname, sizeof(u->sysname), "Zanop");
    str_copy(u->nodename, sizeof(u->nodename), "zanop");
    str_copy(u->release, sizeof(u->release), "0.0.6");
    str_copy(u->version, sizeof(u->version), "zanop-kernel");
    str_copy(u->machine, sizeof(u->machine), "x86_64");
    str_copy(u->domainname, sizeof(u->domainname), "(none)");
    return 0;
}

i64 sys_kill(i64 pid, i64 sig) {
    if (sig < 0 || sig > 64) return -err::INVAL;
    if (pid <= 0) return -err::SRCH; // process groups: not supported
    if (!pid_is_alive(static_cast<u32>(pid))) return -err::SRCH;
    if (sig == 0) return 0;
    return be->proc_kill(static_cast<u32>(pid), static_cast<int>(sig));
}

i64 sys_clock_gettime(i64 clock, timespec* ts) {
    if (!be->clock_gettime) return -err::NOSYS;
    if (!user_ok(ts, sizeof(*ts))) return -err::FAULT;
    if (clock != 0 && clock != 1) return -err::INVAL;
    return be->clock_gettime(static_cast<int>(clock), ts);
}

} // namespace

void init(const Backend* backend) {
    be = backend;
    zero(ofds, sizeof(ofds));
    zero(procs, sizeof(procs));
    zero(maps, sizeof(maps));
    umask_value = 022;
    in_user = false;
    ofds[0].kind = Kind::ConsoleIn;
    ofds[0].flags = oflag::RDONLY;
    ofds[0].refs = 1;
    ofds[1].kind = Kind::ConsoleOut;
    ofds[1].flags = oflag::WRONLY;
    ofds[1].refs = 1;
    ofds[2].kind = Kind::ConsoleOut;
    ofds[2].flags = oflag::WRONLY;
    ofds[2].refs = 1;
}

void enter_user_context(u32 pid, u64 lo, u64 hi) {
    in_user = true;
    user_pid = pid;
    user_lo = lo;
    user_hi = hi;
}

void leave_user_context() {
    if (in_user) reap(user_pid);
    in_user = false;
    user_pid = 0;
}

bool in_user_context() { return in_user; }

void reap(u32 pid) {
    for (Proc& p : procs)
        if (p.used && p.pid == pid) drop(p);
}

i64 syscall(u64 number, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a6;
    if (!be) return -err::NOSYS;
    const u32 pid = current_pid();
    if (number == nr::GETPID || number == nr::SET_TID_ADDRESS) return pid;
    if (number == nr::GETPPID) return 1;
    Proc* pp = proc_for(pid);
    if (!pp) return -err::AGAIN;
    Proc& p = *pp;

    switch (number) {
    case nr::READ: return sys_read(p, static_cast<i64>(a1), reinterpret_cast<void*>(a2), a3);
    case nr::WRITE: return sys_write(p, static_cast<i64>(a1), reinterpret_cast<const void*>(a2), a3);
    case nr::OPEN: return sys_open(p, reinterpret_cast<const char*>(a1), a2);
    case nr::OPENAT:
        if (static_cast<i64>(static_cast<i32>(a1)) != AT_FDCWD &&
            !(user_ok(reinterpret_cast<const char*>(a2), 1) && reinterpret_cast<const char*>(a2)[0] == '/'))
            return -err::NOSYS; // directory-relative lookup: not supported yet
        return sys_open(p, reinterpret_cast<const char*>(a2), a3);
    case nr::CLOSE: return sys_close(p, static_cast<i64>(a1));
    case nr::STAT:
    case nr::LSTAT: return stat_path(p, reinterpret_cast<const char*>(a1), reinterpret_cast<Stat*>(a2));
    case nr::FSTAT: return sys_fstat(p, static_cast<i64>(a1), reinterpret_cast<Stat*>(a2));
    case nr::NEWFSTATAT:
        if (static_cast<i64>(static_cast<i32>(a1)) != AT_FDCWD &&
            !(user_ok(reinterpret_cast<const char*>(a2), 1) && reinterpret_cast<const char*>(a2)[0] == '/'))
            return -err::NOSYS;
        return stat_path(p, reinterpret_cast<const char*>(a2), reinterpret_cast<Stat*>(a3));
    case nr::LSEEK: return sys_lseek(p, static_cast<i64>(a1), static_cast<i64>(a2), static_cast<i64>(a3));
    case nr::PREAD64: return sys_pread(p, static_cast<i64>(a1), reinterpret_cast<void*>(a2), a3, static_cast<i64>(a4), false);
    case nr::PWRITE64: return sys_pread(p, static_cast<i64>(a1), const_cast<void*>(reinterpret_cast<const void*>(a2)), a3, static_cast<i64>(a4), true);
    case nr::READV: return sys_vector(p, static_cast<i64>(a1), reinterpret_cast<const IoVec*>(a2), static_cast<i64>(a3), false);
    case nr::WRITEV: return sys_vector(p, static_cast<i64>(a1), reinterpret_cast<const IoVec*>(a2), static_cast<i64>(a3), true);
    case nr::ACCESS: return sys_access(p, reinterpret_cast<const char*>(a1));
    case nr::DUP: return dup_to(p, static_cast<i64>(a1), 0, false);
    case nr::DUP2: return sys_dup2(p, static_cast<i64>(a1), static_cast<i64>(a2));
    case nr::FCNTL: return sys_fcntl(p, static_cast<i64>(a1), static_cast<i64>(a2), a3);
    case nr::IOCTL: return sys_ioctl(p, static_cast<i64>(a1), a2, a3);
    case nr::FTRUNCATE: return sys_ftruncate(p, static_cast<i64>(a1), static_cast<i64>(a2));
    case nr::GETCWD: return sys_getcwd(p, reinterpret_cast<char*>(a1), a2);
    case nr::CHDIR: return sys_chdir(p, reinterpret_cast<const char*>(a1));
    case nr::RENAME: return sys_rename(p, reinterpret_cast<const char*>(a1), reinterpret_cast<const char*>(a2));
    case nr::MKDIR: return sys_mkdir(p, reinterpret_cast<const char*>(a1));
    case nr::RMDIR: return sys_rmdir(p, reinterpret_cast<const char*>(a1));
    case nr::UNLINK: return sys_unlink(p, reinterpret_cast<const char*>(a1));
    case nr::GETDENTS64: return sys_getdents(p, static_cast<i64>(a1), reinterpret_cast<u8*>(a2), a3);
    case nr::MMAP: return sys_mmap(a2, a4, static_cast<i64>(static_cast<i32>(a5)));
    case nr::MPROTECT: return 0; // no memory protection yet: accepted and ignored
    case nr::MUNMAP: return sys_munmap(a1, a2);
    case nr::BRK: return -err::NOMEM; // libc falls back to mmap
    case nr::SCHED_YIELD: be->proc_yield(); return 0;
    case nr::UNAME: return sys_uname(reinterpret_cast<UtsName*>(a1));
    case nr::KILL: return sys_kill(static_cast<i64>(static_cast<i32>(a1)), static_cast<i64>(static_cast<i32>(a2)));
    case nr::EXIT:
    case nr::EXIT_GROUP: {
        if (in_user) { // the ring-3 entry code ends the program; just drop its state
            reap(pid);
            return 0;
        }
        const i64 r = be->proc_exit(pid, static_cast<int>(a1));
        if (r >= 0) reap(pid);
        return r;
    }
    case nr::UMASK: {
        const u32 old = umask_value;
        umask_value = static_cast<u32>(a1) & 0777;
        return old;
    }
    case nr::GETUID:
    case nr::GETGID:
    case nr::GETEUID:
    case nr::GETEGID: return 0;
    case nr::CLOCK_GETTIME: return sys_clock_gettime(static_cast<i64>(a1), reinterpret_cast<timespec*>(a2));
    default: return -err::NOSYS;
    }
}

} // namespace posix
