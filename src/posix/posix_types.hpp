// POSIX layer: ABI types and constants.
//
// Numbers and struct layouts deliberately match Linux x86-64 so that a
// stock musl libc can be pointed at Zanop OS with only a thin syscall
// stub. This header has no includes so it compiles both freestanding
// (kernel) and hosted (unit tests). Names live in sub-namespaces rather
// than being spelled ENOENT/O_CREAT, so they never collide with the
// macros of a hosted <errno.h>/<fcntl.h>.
#pragma once

namespace posix {

using i8 = __INT8_TYPE__;
using i16 = __INT16_TYPE__;
using i32 = __INT32_TYPE__;
using i64 = __INT64_TYPE__;
using u8 = __UINT8_TYPE__;
using u16 = __UINT16_TYPE__;
using u32 = __UINT32_TYPE__;
using u64 = __UINT64_TYPE__;
using usize = __SIZE_TYPE__;

// Largest absolute path (including the terminating NUL) the Zanop
// filesystem can store. This is FS NAME_SIZE in src/fs/fs.cpp.
constexpr usize PATH_LIMIT = 48;
constexpr usize NAME_LIMIT = 47;

// Error numbers (Linux values). Syscalls return -err on failure.
namespace err {
constexpr int PERM = 1;
constexpr int NOENT = 2;
constexpr int SRCH = 3;
constexpr int INTR = 4;
constexpr int IO = 5;
constexpr int BADF = 9;
constexpr int AGAIN = 11;
constexpr int NOMEM = 12;
constexpr int ACCES = 13;
constexpr int FAULT = 14;
constexpr int BUSY = 16;
constexpr int EXIST = 17;
constexpr int NODEV = 19;
constexpr int NOTDIR = 20;
constexpr int ISDIR = 21;
constexpr int INVAL = 22;
constexpr int NFILE = 23;
constexpr int MFILE = 24;
constexpr int NOTTY = 25;
constexpr int FBIG = 27;
constexpr int NOSPC = 28;
constexpr int SPIPE = 29;
constexpr int ROFS = 30;
constexpr int RANGE = 34;
constexpr int NAMETOOLONG = 36;
constexpr int NOSYS = 38;
constexpr int NOTEMPTY = 39;
} // namespace err

// open(2) flags.
namespace oflag {
constexpr u32 RDONLY = 0;
constexpr u32 WRONLY = 1;
constexpr u32 RDWR = 2;
constexpr u32 ACCMODE = 3;
constexpr u32 CREAT = 0100;
constexpr u32 EXCL = 0200;
constexpr u32 NOCTTY = 0400;
constexpr u32 TRUNC = 01000;
constexpr u32 APPEND = 02000;
constexpr u32 NONBLOCK = 04000;
constexpr u32 DIRECTORY = 0200000;
constexpr u32 CLOEXEC = 02000000;
} // namespace oflag

namespace seek {
constexpr int SET = 0;
constexpr int CUR = 1;
constexpr int END = 2;
} // namespace seek

namespace fcmd {
constexpr int DUPFD = 0;
constexpr int GETFD = 1;
constexpr int SETFD = 2;
constexpr int GETFL = 3;
constexpr int SETFL = 4;
constexpr int DUPFD_CLOEXEC = 1030;
constexpr int FD_CLOEXEC = 1;
} // namespace fcmd

namespace amode {
constexpr int F = 0;
constexpr int X = 1;
constexpr int W = 2;
constexpr int R = 4;
} // namespace amode

namespace mode {
constexpr u32 IFMT = 0170000;
constexpr u32 IFDIR = 0040000;
constexpr u32 IFCHR = 0020000;
constexpr u32 IFREG = 0100000;
} // namespace mode

namespace dtype {
constexpr u8 CHR = 2;
constexpr u8 DIR = 4;
constexpr u8 REG = 8;
} // namespace dtype

namespace mmapf {
constexpr int PRIVATE = 0x02;
constexpr int ANONYMOUS = 0x20;
} // namespace mmapf

constexpr int AT_FDCWD = -100;

namespace ioc {
constexpr u64 TCGETS = 0x5401;
constexpr u64 TIOCGWINSZ = 0x5413;
} // namespace ioc

struct timespec {
    i64 tv_sec;
    i64 tv_nsec;
};

// Linux x86-64 `struct stat`.
struct Stat {
    u64 st_dev;
    u64 st_ino;
    u64 st_nlink;
    u32 st_mode;
    u32 st_uid;
    u32 st_gid;
    i32 pad0;
    u64 st_rdev;
    i64 st_size;
    i64 st_blksize;
    i64 st_blocks;
    timespec st_atim;
    timespec st_mtim;
    timespec st_ctim;
    i64 unused[3];
};
static_assert(sizeof(Stat) == 144, "Stat must match Linux x86-64 struct stat");

// Linux `struct linux_dirent64` header; the name follows the header.
struct Dirent64 {
    u64 d_ino;
    i64 d_off;
    u16 d_reclen;
    u8 d_type;
    char d_name[1];
};
constexpr usize DIRENT64_HEADER = 19;

struct IoVec {
    void* iov_base;
    usize iov_len;
};

struct WinSize {
    u16 ws_row;
    u16 ws_col;
    u16 ws_xpixel;
    u16 ws_ypixel;
};

struct UtsName {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

// Syscall numbers (Linux x86-64).
namespace nr {
constexpr u64 READ = 0;
constexpr u64 WRITE = 1;
constexpr u64 OPEN = 2;
constexpr u64 CLOSE = 3;
constexpr u64 STAT = 4;
constexpr u64 FSTAT = 5;
constexpr u64 LSTAT = 6;
constexpr u64 LSEEK = 8;
constexpr u64 MMAP = 9;
constexpr u64 MPROTECT = 10;
constexpr u64 MUNMAP = 11;
constexpr u64 BRK = 12;
constexpr u64 IOCTL = 16;
constexpr u64 PREAD64 = 17;
constexpr u64 PWRITE64 = 18;
constexpr u64 READV = 19;
constexpr u64 WRITEV = 20;
constexpr u64 ACCESS = 21;
constexpr u64 SCHED_YIELD = 24;
constexpr u64 DUP = 32;
constexpr u64 DUP2 = 33;
constexpr u64 GETPID = 39;
constexpr u64 EXIT = 60;
constexpr u64 KILL = 62;
constexpr u64 UNAME = 63;
constexpr u64 FCNTL = 72;
constexpr u64 FTRUNCATE = 77;
constexpr u64 GETCWD = 79;
constexpr u64 CHDIR = 80;
constexpr u64 RENAME = 82;
constexpr u64 MKDIR = 83;
constexpr u64 RMDIR = 84;
constexpr u64 UNLINK = 87;
constexpr u64 UMASK = 95;
constexpr u64 GETUID = 102;
constexpr u64 GETGID = 104;
constexpr u64 GETEUID = 107;
constexpr u64 GETEGID = 108;
constexpr u64 GETPPID = 110;
constexpr u64 GETDENTS64 = 217;
constexpr u64 SET_TID_ADDRESS = 218;
constexpr u64 CLOCK_GETTIME = 228;
constexpr u64 EXIT_GROUP = 231;
constexpr u64 OPENAT = 257;
constexpr u64 NEWFSTATAT = 262;
} // namespace nr

} // namespace posix
