// Host-side tests for the portable POSIX core. Runs against an in-memory
// backend, so no kernel or emulator is needed:  make test-posix
#include "posix/posix.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace posix;

// ---- ABI layout (must match Linux x86-64 so musl works unmodified) ---------
static_assert(sizeof(Stat) == 144, "struct stat");
static_assert(offsetof(Stat, st_mode) == 24, "st_mode");
static_assert(offsetof(Stat, st_size) == 48, "st_size");
static_assert(offsetof(Stat, st_blocks) == 64, "st_blocks");
static_assert(offsetof(Stat, st_atim) == 72, "st_atim");
static_assert(sizeof(UtsName) == 390, "struct utsname");
static_assert(sizeof(IoVec) == 16, "struct iovec");
static_assert(DIRENT64_HEADER == offsetof(Dirent64, d_name), "dirent64 header");

// ---- in-memory backend ------------------------------------------------------
namespace mock {
std::map<std::string, std::string> files;
std::set<std::string> dirs;
std::string console_out;
std::string console_in;
u32 pid = 100;
std::set<u32> alive;
std::vector<std::pair<u32, int>> exits;
std::vector<std::pair<u32, int>> kills;
std::set<void*> live_allocs;
bool clock_enabled = false;

std::string parent(const std::string& p) {
    const size_t slash = p.rfind('/');
    return slash == 0 ? "/" : p.substr(0, slash);
}
std::string leaf(const std::string& p) { return p.substr(p.rfind('/') + 1); }

i64 stat(const char* path, FsStat* out) {
    if (dirs.count(path)) { out->is_dir = true; out->size = 0; return 0; }
    auto it = files.find(path);
    if (it == files.end()) return -err::NOENT;
    out->is_dir = false;
    out->size = it->second.size();
    return 0;
}
i64 read(const char* path, u64 off, void* buf, usize n) {
    const std::string& s = files.at(path);
    if (off >= s.size()) return 0;
    n = std::min<usize>(n, s.size() - off);
    memcpy(buf, s.data() + off, n);
    return static_cast<i64>(n);
}
i64 write(const char* path, u64 off, const void* buf, usize n) {
    std::string& s = files.at(path);
    if (s.size() < off + n) s.resize(off + n, '\0');
    memcpy(&s[off], buf, n);
    return static_cast<i64>(n);
}
i64 truncate(const char* path, u64 size) { files.at(path).resize(size, '\0'); return 0; }
i64 create(const char* path) { files[path] = ""; return 0; }
i64 unlink(const char* path) { files.erase(path); return 0; }
i64 mkdir(const char* path) { dirs.insert(path); return 0; }
i64 rmdir(const char* path) { dirs.erase(path); return 0; }
i64 rename(const char* a, const char* b) {
    files[b] = files.at(a);
    files.erase(a);
    return 0;
}
i64 readdir(const char* path, usize index, DirEntry* out) {
    std::vector<std::pair<std::string, bool>> kids;
    for (const auto& d : dirs) if (d != "/" && parent(d) == path) kids.push_back({leaf(d), true});
    for (const auto& f : files) if (parent(f.first) == path) kids.push_back({leaf(f.first), false});
    if (index >= kids.size()) return 0;
    strncpy(out->name, kids[index].first.c_str(), PATH_LIMIT - 1);
    out->name[PATH_LIMIT - 1] = 0;
    out->is_dir = kids[index].second;
    return 1;
}
i64 con_read(void* buf, usize n) {
    n = std::min(n, console_in.size());
    memcpy(buf, console_in.data(), n);
    console_in.erase(0, n);
    return static_cast<i64>(n);
}
i64 con_write(const void* buf, usize n) {
    console_out.append(static_cast<const char*>(buf), n);
    return static_cast<i64>(n);
}
u32 cur_pid() { return pid; }
bool pid_alive(u32 p) { return alive.count(p) != 0; }
i64 proc_exit(u32 p, int status) { exits.push_back({p, status}); alive.erase(p); return 0; }
i64 proc_kill(u32 p, int sig) { kills.push_back({p, sig}); return 0; }
i64 proc_yield() { return 0; }
void* mem_alloc(usize size, usize align) {
    void* p = aligned_alloc(align, (size + align - 1) / align * align);
    if (p) { memset(p, 0xAB, size); live_allocs.insert(p); } // dirty, so zeroing is tested
    return p;
}
void mem_free(void* p) { live_allocs.erase(p); free(p); }
i64 clock_gettime(int id, posix::timespec* ts) { ts->tv_sec = 1000 + id; ts->tv_nsec = 5; return 0; }

Backend backend = {stat, read, write, truncate, create, unlink, mkdir, rmdir, rename, readdir,
                   con_read, con_write, cur_pid, pid_alive, proc_exit, proc_kill, proc_yield,
                   mem_alloc, mem_free, nullptr};

void reset() {
    files.clear();
    dirs = {"/"};
    console_out.clear();
    console_in.clear();
    pid = 100;
    alive = {100};
    exits.clear();
    kills.clear();
    backend.clock_gettime = clock_enabled ? clock_gettime : nullptr;
    posix::init(&backend);
}
} // namespace mock

// ---- test harness -------------------------------------------------------------
static int failures = 0;
static int checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; const long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { ++failures; printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

static u64 A(const void* p) { return reinterpret_cast<u64>(p); }
static i64 sc(u64 n, u64 a1 = 0, u64 a2 = 0, u64 a3 = 0, u64 a4 = 0, u64 a5 = 0) {
    return posix::syscall(n, a1, a2, a3, a4, a5, 0);
}
static i64 sys_open(const char* p, u32 flags) { return sc(nr::OPEN, A(p), flags, 0644); }
static i64 sys_write(i64 fd, const std::string& s) { return sc(nr::WRITE, fd, A(s.data()), s.size()); }
static std::string read_all(i64 fd) {
    std::string out;
    char buf[7]; // deliberately small
    for (;;) {
        const i64 r = sc(nr::READ, fd, A(buf), sizeof(buf));
        if (r <= 0) break;
        out.append(buf, r);
    }
    return out;
}
static void put_file(const char* path, const std::string& content) { mock::files[path] = content; }

// ---- tests --------------------------------------------------------------------

static void test_console() {
    mock::reset();
    CHECK_EQ(sys_write(1, "hello "), 6);
    CHECK_EQ(sys_write(2, "err"), 3);
    CHECK(mock::console_out == "hello err");
    IoVec iov[3] = {{const_cast<char*>("ab"), 2}, {const_cast<char*>(""), 0}, {const_cast<char*>("cde"), 3}};
    CHECK_EQ(sc(nr::WRITEV, 1, A(iov), 3), 5);
    CHECK(mock::console_out == "hello errabcde");
    mock::console_in = "line\nrest";
    char buf[16];
    CHECK_EQ(sc(nr::READ, 0, A(buf), sizeof(buf)), 9);
    CHECK_EQ(sc(nr::READ, 1, A(buf), 4), -err::BADF); // stdout is write-only
    CHECK_EQ(sc(nr::WRITE, 0, A("x"), 1), -err::BADF); // stdin is read-only
    CHECK_EQ(sc(nr::LSEEK, 1, 0, seek::SET), -err::SPIPE);
    CHECK_EQ(sc(nr::WRITE, 99, A("x"), 1), -err::BADF);
    CHECK_EQ(sc(nr::WRITE, static_cast<u64>(-1), A("x"), 1), -err::BADF);
}

static void test_file_io() {
    mock::reset();
    CHECK_EQ(sys_open("/nope", oflag::RDONLY), -err::NOENT);
    const i64 fd = sys_open("/a.txt", oflag::RDWR | oflag::CREAT);
    CHECK_EQ(fd, 3); // lowest free descriptor
    CHECK_EQ(sys_write(fd, "hello world"), 11);
    CHECK_EQ(sc(nr::LSEEK, fd, 0, seek::CUR), 11);
    CHECK_EQ(sc(nr::LSEEK, fd, 0, seek::SET), 0);
    CHECK(read_all(fd) == "hello world");
    CHECK_EQ(sc(nr::LSEEK, fd, static_cast<u64>(-5), seek::END), 6);
    CHECK(read_all(fd) == "world");
    CHECK_EQ(sc(nr::LSEEK, fd, static_cast<u64>(-1), seek::SET), -err::INVAL);
    CHECK_EQ(sc(nr::LSEEK, fd, 0, 9), -err::INVAL);

    // Writing past EOF leaves a zero-filled hole.
    CHECK_EQ(sc(nr::LSEEK, fd, 14, seek::SET), 14);
    CHECK_EQ(sys_write(fd, "!"), 1);
    CHECK_EQ(mock::files["/a.txt"].size(), 15);
    CHECK_EQ(mock::files["/a.txt"][12], 0);
    CHECK_EQ(sc(nr::CLOSE, fd), 0);
    CHECK_EQ(sc(nr::CLOSE, fd), -err::BADF);

    // Access modes are enforced.
    const i64 ro = sys_open("/a.txt", oflag::RDONLY);
    CHECK_EQ(sys_write(ro, "x"), -err::BADF);
    const i64 wo = sys_open("/a.txt", oflag::WRONLY);
    char c;
    CHECK_EQ(sc(nr::READ, wo, A(&c), 1), -err::BADF);
    CHECK_EQ(sys_open("/a.txt", 3), -err::INVAL);

    // O_EXCL, O_TRUNC, O_APPEND.
    CHECK_EQ(sys_open("/a.txt", oflag::CREAT | oflag::EXCL | oflag::WRONLY), -err::EXIST);
    put_file("/log", "abc");
    const i64 ap = sys_open("/log", oflag::WRONLY | oflag::APPEND);
    CHECK_EQ(sc(nr::LSEEK, ap, 0, seek::SET), 0);
    CHECK_EQ(sys_write(ap, "def"), 3); // appends regardless of offset
    CHECK(mock::files["/log"] == "abcdef");
    const i64 tr = sys_open("/log", oflag::WRONLY | oflag::TRUNC);
    CHECK(tr >= 0);
    CHECK(mock::files["/log"].empty());

    // Missing parent / parent not a directory.
    CHECK_EQ(sys_open("/no/such/f", oflag::WRONLY | oflag::CREAT), -err::NOENT);
    CHECK_EQ(sys_open("/a.txt/f", oflag::WRONLY | oflag::CREAT), -err::NOTDIR);

    // Positional I/O does not move the shared offset.
    put_file("/p", "0123456789");
    const i64 pf = sys_open("/p", oflag::RDWR);
    char b4[5] = {0};
    CHECK_EQ(sc(nr::PREAD64, pf, A(b4), 4, 3), 4);
    CHECK(std::string(b4) == "3456");
    CHECK_EQ(sc(nr::LSEEK, pf, 0, seek::CUR), 0);
    CHECK_EQ(sc(nr::PWRITE64, pf, A("XY"), 2, 8), 2);
    CHECK(mock::files["/p"] == "01234567XY");
    CHECK_EQ(sc(nr::LSEEK, pf, 0, seek::CUR), 0);
    CHECK_EQ(sc(nr::PREAD64, pf, A(b4), 4, static_cast<u64>(-1)), -err::INVAL);

    // readv
    sc(nr::LSEEK, pf, 0, seek::SET);
    char r1[3], r2[4];
    IoVec iov[2] = {{r1, 3}, {r2, 4}};
    CHECK_EQ(sc(nr::READV, pf, A(iov), 2), 7);
    CHECK(memcmp(r1, "012", 3) == 0 && memcmp(r2, "3456", 4) == 0);

    // ftruncate
    CHECK_EQ(sc(nr::FTRUNCATE, pf, 4), 0);
    CHECK(mock::files["/p"] == "0123");
    CHECK_EQ(sc(nr::FTRUNCATE, ro, 0), -err::INVAL); // not open for writing

    // Null pointers are rejected, not dereferenced.
    CHECK_EQ(sc(nr::OPEN, 0, 0), -err::FAULT);
    CHECK_EQ(sc(nr::READ, pf, 0, 4), -err::FAULT);
    CHECK_EQ(sc(nr::WRITE, pf, 0, 4), -err::FAULT);
    CHECK_EQ(sc(nr::WRITE, pf, 0, 0), 0); // zero-length is fine
}

static void test_paths() {
    mock::reset();
    mock::dirs.insert("/a");
    mock::dirs.insert("/a/b");
    put_file("/a/b/f", "x");
    char buf[PATH_LIMIT];
    CHECK_EQ(sc(nr::GETCWD, A(buf), sizeof(buf)), 2);
    CHECK(std::string(buf) == "/");
    CHECK_EQ(sc(nr::CHDIR, A("a//./b/")), 0);
    CHECK_EQ(sc(nr::GETCWD, A(buf), sizeof(buf)), 5);
    CHECK(std::string(buf) == "/a/b");
    CHECK_EQ(sc(nr::GETCWD, A(buf), 4), -err::RANGE);
    CHECK(sys_open("f", oflag::RDONLY) >= 0); // relative to cwd
    CHECK(sys_open("../b/./f", oflag::RDONLY) >= 0);
    CHECK(sys_open("/../../a/b/f", oflag::RDONLY) >= 0); // ".." at root stays at root
    CHECK_EQ(sc(nr::CHDIR, A("..")), 0);
    CHECK_EQ(sc(nr::GETCWD, A(buf), sizeof(buf)), 3);
    CHECK(std::string(buf) == "/a");
    CHECK_EQ(sc(nr::CHDIR, A("../..")), 0);
    sc(nr::GETCWD, A(buf), sizeof(buf));
    CHECK(std::string(buf) == "/");
    CHECK_EQ(sc(nr::CHDIR, A("/a/b/f")), -err::NOTDIR);
    CHECK_EQ(sc(nr::CHDIR, A("/missing")), -err::NOENT);
    CHECK_EQ(sc(nr::CHDIR, A("")), -err::NOENT);

    // Length limits come from the on-disk name size.
    std::string longpath = "/";
    longpath += std::string(PATH_LIMIT, 'x');
    CHECK_EQ(sys_open(longpath.c_str(), oflag::RDONLY), -err::NAMETOOLONG);
    std::string deep = "/a/b/" + std::string(NAME_LIMIT - 5, 'y'); // exactly PATH_LIMIT - 1 chars
    CHECK_EQ(deep.size(), PATH_LIMIT - 1);
    CHECK_EQ(sys_open(deep.c_str(), oflag::RDONLY), -err::NOENT); // fits, just absent
    CHECK_EQ(sys_open((deep + "z").c_str(), oflag::RDONLY), -err::NAMETOOLONG);

    // access
    CHECK_EQ(sc(nr::ACCESS, A("/a/b/f"), amode::R), 0);
    CHECK_EQ(sc(nr::ACCESS, A("/a/b/g"), amode::F), -err::NOENT);
}

static void test_dup_and_fcntl() {
    mock::reset();
    put_file("/d", "abcdef");
    const i64 fd = sys_open("/d", oflag::RDONLY);
    const i64 d1 = sc(nr::DUP, fd);
    CHECK_EQ(d1, fd + 1);
    char c[2] = {0, 0};
    sc(nr::READ, fd, A(c), 1);
    sc(nr::READ, d1, A(c), 1);
    CHECK_EQ(c[0], 'b'); // duplicates share one offset
    CHECK_EQ(sc(nr::CLOSE, fd), 0);
    sc(nr::READ, d1, A(c), 1);
    CHECK_EQ(c[0], 'c'); // description survives while any descriptor is open

    CHECK_EQ(sc(nr::DUP2, d1, 20), 20);
    CHECK_EQ(sc(nr::DUP2, d1, d1), d1);
    CHECK_EQ(sc(nr::DUP2, 77, 5), -err::BADF);
    CHECK_EQ(sc(nr::DUP2, d1, 99), -err::BADF);
    CHECK_EQ(sc(nr::DUP2, d1, 1), 1); // redirect stdout to the file
    CHECK_EQ(sys_write(1, "x"), -err::BADF); // file was opened read-only

    CHECK_EQ(sc(nr::FCNTL, d1, fcmd::GETFD), 0);
    CHECK_EQ(sc(nr::FCNTL, d1, fcmd::SETFD, fcmd::FD_CLOEXEC), 0);
    CHECK_EQ(sc(nr::FCNTL, d1, fcmd::GETFD), 1);
    CHECK_EQ(sc(nr::FCNTL, d1, fcmd::GETFL) & oflag::ACCMODE, oflag::RDONLY);
    CHECK_EQ(sc(nr::FCNTL, d1, fcmd::SETFL, oflag::NONBLOCK), 0);
    CHECK(sc(nr::FCNTL, d1, fcmd::GETFL) & oflag::NONBLOCK);
    const i64 hi = sc(nr::FCNTL, d1, fcmd::DUPFD, 10);
    CHECK(hi >= 10);
    CHECK_EQ(sc(nr::FCNTL, d1, 9999), -err::INVAL);
    CHECK_EQ(sc(nr::FCNTL, d1, fcmd::DUPFD, 500), -err::INVAL);

    // Descriptor table exhaustion, then recovery.
    mock::reset();
    put_file("/m", "");
    int opened = 0;
    i64 last = 0;
    for (int i = 0; i < 100; ++i) {
        last = sys_open("/m", oflag::RDONLY);
        if (last < 0) break;
        ++opened;
    }
    CHECK_EQ(opened, 29); // 32 descriptors minus stdin/stdout/stderr
    CHECK_EQ(last, -err::MFILE);
    CHECK_EQ(sc(nr::CLOSE, 5), 0);
    CHECK_EQ(sys_open("/m", oflag::RDONLY), 5);

    // The global open-file table is finite too.
    mock::reset();
    put_file("/m", "");
    int total = 0;
    for (u32 p = 1; p <= 16; ++p) {
        mock::pid = p;
        mock::alive.insert(p);
        for (int i = 0; i < 10; ++i) {
            const i64 r = sys_open("/m", oflag::RDONLY);
            if (r == -err::NFILE) goto done;
            CHECK(r >= 0);
            ++total;
        }
    }
done:
    CHECK_EQ(total, 61); // 64 table entries minus the three console descriptions
}

static void test_stat() {
    mock::reset();
    put_file("/s", std::string(1000, 'x'));
    mock::dirs.insert("/d");
    Stat st;
    CHECK_EQ(sc(nr::STAT, A("/s"), A(&st)), 0);
    CHECK_EQ(st.st_size, 1000);
    CHECK_EQ(st.st_blocks, 2);
    CHECK_EQ(st.st_mode & mode::IFMT, mode::IFREG);
    CHECK_EQ(sc(nr::LSTAT, A("/d"), A(&st)), 0);
    CHECK_EQ(st.st_mode & mode::IFMT, mode::IFDIR);
    CHECK_EQ(sc(nr::STAT, A("/zz"), A(&st)), -err::NOENT);
    CHECK_EQ(sc(nr::STAT, A("/s"), 0), -err::FAULT);
    const i64 fd = sys_open("/s", oflag::RDONLY);
    CHECK_EQ(sc(nr::FSTAT, fd, A(&st)), 0);
    CHECK_EQ(st.st_size, 1000);
    CHECK_EQ(sc(nr::FSTAT, 1, A(&st)), 0);
    CHECK_EQ(st.st_mode & mode::IFMT, mode::IFCHR);
    CHECK_EQ(sc(nr::FSTAT, 50, A(&st)), -err::BADF);
    CHECK_EQ(sc(nr::NEWFSTATAT, static_cast<u64>(static_cast<i32>(AT_FDCWD)), A("/s"), A(&st)), 0);
    CHECK_EQ(sc(nr::NEWFSTATAT, 3, A("rel"), A(&st)), -err::NOSYS);
    CHECK(sc(nr::OPENAT, static_cast<u64>(static_cast<i32>(AT_FDCWD)), A("/s"), oflag::RDONLY) >= 0);
    CHECK_EQ(sc(nr::OPENAT, 3, A("rel"), oflag::RDONLY), -err::NOSYS);
}

static void test_namespace_ops() {
    mock::reset();
    CHECK_EQ(sc(nr::MKDIR, A("/x"), 0755), 0);
    CHECK(mock::dirs.count("/x"));
    CHECK_EQ(sc(nr::MKDIR, A("/x"), 0755), -err::EXIST);
    CHECK_EQ(sc(nr::MKDIR, A("/"), 0755), -err::EXIST);
    CHECK_EQ(sc(nr::MKDIR, A("/q/r"), 0755), -err::NOENT);
    put_file("/x/f", "1");
    CHECK_EQ(sc(nr::MKDIR, A("/x/f/g"), 0755), -err::NOTDIR);
    CHECK_EQ(sc(nr::RMDIR, A("/x")), -err::NOTEMPTY);
    CHECK_EQ(sc(nr::RMDIR, A("/")), -err::BUSY);
    CHECK_EQ(sc(nr::RMDIR, A("/x/f")), -err::NOTDIR);
    CHECK_EQ(sc(nr::UNLINK, A("/x")), -err::ISDIR);
    CHECK_EQ(sc(nr::UNLINK, A("/x/f")), 0);
    CHECK_EQ(sc(nr::UNLINK, A("/x/f")), -err::NOENT);
    CHECK_EQ(sc(nr::RMDIR, A("/x")), 0);
    CHECK(!mock::dirs.count("/x"));

    // rename
    put_file("/r1", "one");
    put_file("/r2", "two");
    CHECK_EQ(sc(nr::RENAME, A("/r1"), A("/r2")), 0); // overwrites
    CHECK(mock::files["/r2"] == "one" && !mock::files.count("/r1"));
    CHECK_EQ(sc(nr::RENAME, A("/r2"), A("/r2")), 0);
    CHECK_EQ(sc(nr::RENAME, A("/gone"), A("/y")), -err::NOENT);
    mock::dirs.insert("/dd");
    CHECK_EQ(sc(nr::RENAME, A("/dd"), A("/ee")), -err::NOSYS);
    CHECK_EQ(sc(nr::RENAME, A("/r2"), A("/dd")), -err::ISDIR);
    CHECK_EQ(sc(nr::RENAME, A("/r2"), A("/nodir/z")), -err::NOENT);
}

struct Dent { std::string name; u8 type; };
static std::vector<Dent> list_dir(const char* path, usize bufsize) {
    std::vector<Dent> out;
    const i64 fd = sys_open(path, oflag::RDONLY | oflag::DIRECTORY);
    CHECK(fd >= 0);
    std::vector<u8> buf(bufsize);
    for (;;) {
        const i64 n = sc(nr::GETDENTS64, fd, A(buf.data()), buf.size());
        CHECK(n >= 0);
        if (n <= 0) break;
        for (i64 off = 0; off < n;) {
            u16 reclen;
            memcpy(&reclen, &buf[off + 16], 2);
            CHECK(reclen % 8 == 0);
            out.push_back({std::string(reinterpret_cast<char*>(&buf[off + 19])), buf[off + 18]});
            off += reclen;
        }
    }
    sc(nr::CLOSE, fd);
    return out;
}

static void test_directories() {
    mock::reset();
    mock::dirs.insert("/d");
    put_file("/d/one", "");
    put_file("/d/two", "");
    mock::dirs.insert("/d/sub");
    const auto all = list_dir("/d", 4096);
    CHECK_EQ(all.size(), 5);
    CHECK(all[0].name == "." && all[0].type == dtype::DIR);
    CHECK(all[1].name == ".." && all[1].type == dtype::DIR);
    int files = 0, dirs = 0;
    for (size_t i = 2; i < all.size(); ++i) (all[i].type == dtype::DIR ? dirs : files)++;
    CHECK_EQ(files, 2);
    CHECK_EQ(dirs, 1);
    // A buffer that only fits one entry at a time still enumerates everything.
    CHECK_EQ(list_dir("/d", 32).size(), 5);

    const i64 fd = sys_open("/d", oflag::RDONLY);
    std::vector<u8> tiny(8);
    CHECK_EQ(sc(nr::GETDENTS64, fd, A(tiny.data()), tiny.size()), -err::INVAL);
    // A buffer that fits an entry exactly (header + "." + NUL, padded to 24).
    std::vector<u8> exact(24);
    CHECK_EQ(sc(nr::GETDENTS64, fd, A(exact.data()), exact.size()), 24);
    CHECK_EQ(sc(nr::LSEEK, fd, 0, seek::SET), 0);
    std::vector<u8> big(4096);
    CHECK(sc(nr::GETDENTS64, fd, A(big.data()), big.size()) > 0);
    CHECK_EQ(sc(nr::GETDENTS64, fd, A(big.data()), big.size()), 0); // at end
    CHECK_EQ(sc(nr::LSEEK, fd, 0, seek::SET), 0);                    // rewind
    CHECK(sc(nr::GETDENTS64, fd, A(big.data()), big.size()) > 0);
    CHECK_EQ(sc(nr::LSEEK, fd, 5, seek::SET), -err::INVAL);

    // Directory misuse.
    char c;
    CHECK_EQ(sc(nr::READ, fd, A(&c), 1), -err::ISDIR);
    CHECK_EQ(sys_open("/d", oflag::WRONLY), -err::ISDIR);
    CHECK_EQ(sys_open("/d/one", oflag::RDONLY | oflag::DIRECTORY), -err::NOTDIR);
    const i64 f = sys_open("/d/one", oflag::RDONLY);
    CHECK_EQ(sc(nr::GETDENTS64, f, A(big.data()), big.size()), -err::NOTDIR);
}

static void test_memory() {
    mock::reset();
    const u64 anon = mmapf::ANONYMOUS | mmapf::PRIVATE;
    const i64 a = sc(nr::MMAP, 0, 10000, 3, anon, static_cast<u64>(-1));
    CHECK(a > 0);
    CHECK_EQ(static_cast<u64>(a) % 4096, 0);
    const u8* p = reinterpret_cast<const u8*>(a);
    bool zeroed = true;
    for (int i = 0; i < 12288; ++i) zeroed = zeroed && p[i] == 0;
    CHECK(zeroed);
    CHECK_EQ(mock::live_allocs.size(), 1);
    CHECK_EQ(sc(nr::MUNMAP, a, 4096), -err::INVAL); // partial unmap unsupported
    CHECK_EQ(sc(nr::MUNMAP, a, 10000), 0);
    CHECK_EQ(mock::live_allocs.size(), 0);
    CHECK_EQ(sc(nr::MUNMAP, a, 10000), -err::INVAL); // double free is refused
    CHECK_EQ(sc(nr::MMAP, 0, 0, 3, anon, static_cast<u64>(-1)), -err::INVAL);
    CHECK_EQ(sc(nr::MMAP, 0, 4096, 3, mmapf::PRIVATE, 3), -err::NODEV); // file mapping
    CHECK_EQ(sc(nr::MMAP, 0, 4096, 3, anon | 0x10, static_cast<u64>(-1)), -err::INVAL); // MAP_FIXED
    CHECK_EQ(sc(nr::MPROTECT, a, 4096, 1), 0);
    CHECK_EQ(sc(nr::BRK, 0), -err::NOMEM);
    for (int i = 0; i < 32; ++i) CHECK(sc(nr::MMAP, 0, 4096, 3, anon, static_cast<u64>(-1)) > 0);
    CHECK_EQ(sc(nr::MMAP, 0, 4096, 3, anon, static_cast<u64>(-1)), -err::NOMEM); // map table full
    CHECK_EQ(mock::live_allocs.size(), 32); // and nothing leaked on failure
    for (void* q : std::set<void*>(mock::live_allocs)) sc(nr::MUNMAP, A(q), 4096);
}

static void test_terminal_and_misc() {
    mock::reset();
    char t[64];
    CHECK_EQ(sc(nr::IOCTL, 1, ioc::TCGETS, A(t)), 0); // isatty(1)
    CHECK_EQ(sc(nr::IOCTL, 0, ioc::TCGETS, A(t)), 0);
    put_file("/f", "");
    const i64 fd = sys_open("/f", oflag::RDONLY);
    CHECK_EQ(sc(nr::IOCTL, fd, ioc::TCGETS, A(t)), -err::NOTTY); // isatty(file) is false
    WinSize ws = {};
    CHECK_EQ(sc(nr::IOCTL, 1, ioc::TIOCGWINSZ, A(&ws)), 0);
    CHECK(ws.ws_row == 25 && ws.ws_col == 80);
    CHECK_EQ(sc(nr::IOCTL, 1, 0xDEAD, 0), -err::INVAL);

    UtsName u;
    CHECK_EQ(sc(nr::UNAME, A(&u)), 0);
    CHECK(std::string(u.sysname) == "Zanop" && std::string(u.machine) == "x86_64");
    CHECK_EQ(sc(nr::UNAME, 0), -err::FAULT);

    CHECK_EQ(sc(nr::GETPID), 100);
    CHECK_EQ(sc(nr::GETPPID), 1);
    CHECK_EQ(sc(nr::GETUID), 0);
    CHECK_EQ(sc(nr::UMASK, 077), 022);
    CHECK_EQ(sc(nr::UMASK, 022), 077);
    CHECK_EQ(sc(nr::SCHED_YIELD), 0);
    CHECK_EQ(sc(9999), -err::NOSYS);

    // Signals.
    mock::alive.insert(7);
    CHECK_EQ(sc(nr::KILL, 7, 0), 0);   // existence probe
    CHECK_EQ(sc(nr::KILL, 8, 0), -err::SRCH);
    CHECK_EQ(sc(nr::KILL, 7, 15), 0);
    CHECK_EQ(mock::kills.size(), 1);
    CHECK_EQ(sc(nr::KILL, 7, 99), -err::INVAL);
    CHECK_EQ(sc(nr::KILL, static_cast<u64>(-1), 9), -err::SRCH);

    // Clock is unavailable until the backend provides one.
    posix::timespec ts;
    CHECK_EQ(sc(nr::CLOCK_GETTIME, 0, A(&ts)), -err::NOSYS);
    mock::clock_enabled = true;
    mock::reset();
    CHECK_EQ(sc(nr::CLOCK_GETTIME, 1, A(&ts)), 0);
    CHECK_EQ(ts.tv_sec, 1001);
    CHECK_EQ(sc(nr::CLOCK_GETTIME, 5, A(&ts)), -err::INVAL);
    CHECK_EQ(sc(nr::CLOCK_GETTIME, 0, 0), -err::FAULT);
    mock::clock_enabled = false;
}

static void test_processes() {
    mock::reset();
    mock::dirs.insert("/w");
    put_file("/shared", "s");

    // Each process has its own cwd and descriptors.
    mock::pid = 1; mock::alive.insert(1);
    sc(nr::CHDIR, A("/w"));
    const i64 fd1 = sys_open("/shared", oflag::RDONLY);
    mock::pid = 2; mock::alive.insert(2);
    char buf[PATH_LIMIT];
    sc(nr::GETCWD, A(buf), sizeof(buf));
    CHECK(std::string(buf) == "/");
    CHECK_EQ(sc(nr::CLOSE, fd1), -err::BADF);
    mock::pid = 1;
    sc(nr::GETCWD, A(buf), sizeof(buf));
    CHECK(std::string(buf) == "/w");
    CHECK_EQ(sc(nr::CLOSE, fd1), 0);

    // exit tears down the process's state and tells the kernel.
    const i64 held = sys_open("/shared", oflag::RDONLY);
    CHECK(held >= 0);
    CHECK_EQ(sc(nr::EXIT_GROUP, 3), 0);
    CHECK_EQ(mock::exits.size(), 1);
    CHECK_EQ(mock::exits[0].second, 3);
    mock::alive.insert(1);
    sc(nr::GETCWD, A(buf), sizeof(buf));
    CHECK(std::string(buf) == "/"); // fresh state after exit

    // A full process table reclaims entries of processes that are gone.
    mock::reset();
    for (u32 p = 1; p <= 16; ++p) { mock::pid = p; mock::alive.insert(p); sc(nr::SCHED_YIELD); sc(nr::CLOSE, 99); }
    mock::pid = 17;
    CHECK_EQ(sc(nr::CLOSE, 99), -err::AGAIN); // 16 live processes: table full
    mock::alive.erase(5);
    CHECK_EQ(sc(nr::CLOSE, 99), -err::BADF);  // pid 5's slot was reclaimed
    CHECK_EQ(sc(nr::GETPID), 17);             // getpid never needs a slot
}

int main() {
    test_console();
    test_file_io();
    test_paths();
    test_dup_and_fcntl();
    test_stat();
    test_namespace_ops();
    test_directories();
    test_memory();
    test_terminal_and_misc();
    test_processes();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
