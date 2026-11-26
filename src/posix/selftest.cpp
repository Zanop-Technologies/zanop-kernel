// Boot-time self-test for the POSIX layer and ring-3 support.
// Built only by `make selftest`; reports on the serial port (COM1).
//
//   1. kernel-mode calls into posix::syscall() against a real, freshly
//      formatted disk (needs an IDE disk attached to the VM);
//   2. small user programs run in ring 3 and make real SYSCALLs, including
//      ones that must be refused or must fault.
#include "selftest.hpp"
#include "posix.hpp"
#include "../arch/x86_64/usermode.hpp"
#include "../drivers/ata.hpp"
#include "../drivers/serial.hpp"
#include "../fs/fs.hpp"

extern "C" {
extern const uint8_t user_file_start[], user_file_end[];
extern const uint8_t user_badptr_start[], user_badptr_end[];
extern const uint8_t user_pagefault_start[], user_pagefault_end[];
extern const uint8_t user_privileged_start[], user_privileged_end[];
}

namespace posix {
namespace {

int failures = 0;
int passes = 0;

void out(const char* s) { serial::write_string(s); }

void num(long long v) {
    char digits[24];
    int n = 0;
    const bool negative = v < 0;
    unsigned long long u = negative ? 0ull - static_cast<unsigned long long>(v)
                                    : static_cast<unsigned long long>(v);
    do {
        digits[n++] = static_cast<char>('0' + u % 10);
        u /= 10;
    } while (u);
    if (negative) serial::write('-');
    while (n) serial::write(digits[--n]);
}

void check(const char* what, long long got, long long want) {
    const bool ok = got == want;
    out(ok ? "PASS " : "FAIL ");
    out(what);
    out(" = ");
    num(got);
    if (!ok) {
        out(" (wanted ");
        num(want);
        out(")");
        ++failures;
    } else {
        ++passes;
    }
    out("\r\n");
}

i64 sc(u64 n, u64 a = 0, u64 b = 0, u64 c = 0, u64 d = 0, u64 e = 0) {
    return posix::syscall(n, a, b, c, d, e, 0);
}

u64 addr(const void* p) { return reinterpret_cast<u64>(p); }

void kernel_mode_tests() {
    out("-- kernel-mode syscalls --\r\n");
    out("ata devices: ");
    num(static_cast<long long>(Drivers::ATA::device_count()));
    out("\r\n");
    if (!FS::is_mounted() && Drivers::ATA::device_count() > 0) {
        out("formatting disk 0: ");
        num(FS::format(0));
        out("\r\n");
    }
    check("filesystem mounted", FS::is_mounted(), 1);

    UtsName u;
    sc(nr::UNAME, addr(&u));
    out("uname: ");
    out(u.sysname);
    out(" ");
    out(u.release);
    out(" ");
    out(u.machine);
    out("\r\n");

    check("mkdir /t", sc(nr::MKDIR, addr("/t"), 0755), 0);
    check("mkdir /t again -> EEXIST", sc(nr::MKDIR, addr("/t"), 0755), -err::EXIST);
    const i64 fd = sc(nr::OPEN, addr("/t/a.txt"), oflag::RDWR | oflag::CREAT, 0644);
    check("open /t/a.txt (create)", fd >= 3, 1);
    const char msg[] = "hello from posix";
    check("write 16 bytes", sc(nr::WRITE, fd, addr(msg), 16), 16);
    check("lseek 0", sc(nr::LSEEK, fd, 0, seek::SET), 0);
    char buf[32] = {0};
    check("read 16 bytes", sc(nr::READ, fd, addr(buf), 16), 16);
    bool same = true;
    for (int i = 0; i < 16; ++i)
        if (buf[i] != msg[i]) same = false;
    check("read-back matches", same, 1);
    Stat st;
    sc(nr::FSTAT, fd, addr(&st));
    check("fstat size", st.st_size, 16);
    check("pwrite at 20", sc(nr::PWRITE64, fd, addr("XY"), 2, 20), 2);
    sc(nr::FSTAT, fd, addr(&st));
    check("size after pwrite", st.st_size, 22);
    check("ftruncate 5", sc(nr::FTRUNCATE, fd, 5), 0);
    sc(nr::FSTAT, fd, addr(&st));
    check("size after truncate", st.st_size, 5);
    check("close", sc(nr::CLOSE, fd), 0);
    check("close again -> EBADF", sc(nr::CLOSE, fd), -err::BADF);
    check("rename a.txt -> b.txt", sc(nr::RENAME, addr("/t/a.txt"), addr("/t/b.txt")), 0);
    check("stat old name -> ENOENT", sc(nr::STAT, addr("/t/a.txt"), addr(&st)), -err::NOENT);
    const i64 dfd = sc(nr::OPEN, addr("/t"), oflag::RDONLY | oflag::DIRECTORY);
    u8 dirents[256];
    const i64 n = sc(nr::GETDENTS64, dfd, addr(dirents), sizeof(dirents));
    check("getdents64 returns entries", n > 0, 1);
    sc(nr::CLOSE, dfd);
    check("chdir /t", sc(nr::CHDIR, addr("/t")), 0);
    check("rmdir non-empty -> ENOTEMPTY", sc(nr::RMDIR, addr("/t")), -err::NOTEMPTY);
    sc(nr::CHDIR, addr("/"));
    check("unlink b.txt", sc(nr::UNLINK, addr("/t/b.txt")), 0);
    check("rmdir /t", sc(nr::RMDIR, addr("/t")), 0);
    const i64 m = sc(nr::MMAP, 0, 8192, 3, mmapf::ANONYMOUS | mmapf::PRIVATE, static_cast<u64>(-1));
    check("mmap 8192", m > 0, 1);
    check("munmap", sc(nr::MUNMAP, m, 8192), 0);
    check("unknown syscall -> ENOSYS", sc(9999), -err::NOSYS);
}

// Copies a program to the start of the user region and runs it in ring 3.
long long run_user(const uint8_t* start, const uint8_t* end) {
    using namespace Arch::UserMode;
    uint8_t* base = reinterpret_cast<uint8_t*>(USER_BASE);
    for (uint64_t i = 0; i < USER_SIZE; ++i) base[i] = 0;
    for (const uint8_t* p = start; p < end; ++p) base[p - start] = *p;
    return run(USER_BASE, USER_BASE + USER_SIZE - 64, 9000);
}

bool interrupts_enabled() {
    uint64_t flags;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(flags));
    return (flags & 0x200) != 0;
}

void ring3_tests() {
    out("-- ring 3 --\r\n");
    check("user region made accessible", Arch::UserMode::prepare_region(), 1);

    check("user program: file I/O, regs preserved, getpid (exit code = bytes written)",
          run_user(user_file_start, user_file_end), 30);
    i64 fd = sc(nr::OPEN, addr("/user.txt"), oflag::RDONLY);
    char buf[40] = {0};
    check("file written by user program: read size", sc(nr::READ, fd, addr(buf), sizeof(buf)), 30);
    const char want[] = "Hello from ring 3 via SYSCALL\n";
    bool same = true;
    for (int i = 0; i < 30; ++i)
        if (buf[i] != want[i]) same = false;
    check("file written by user program: content", same, 1);
    sc(nr::CLOSE, fd);

    check("user program run a second time", run_user(user_file_start, user_file_end), 30);
    check("kernel pointers rejected with EFAULT (exit code 14)",
          run_user(user_badptr_start, user_badptr_end), 14);
    check("user read of kernel memory -> page fault (#PF)",
          run_user(user_pagefault_start, user_pagefault_end), -1014);
    check("user privileged instruction -> #GP",
          run_user(user_privileged_start, user_privileged_end), -1013);

    check("kernel still in kernel mode", posix::in_user_context(), 0);
    check("interrupts enabled after returning", interrupts_enabled(), 1);
    check("kernel-mode syscall works afterwards", sc(nr::UNLINK, addr("/user.txt")), 0);
}

} // namespace

void selftest() {
    out("\r\n=== POSIX + ring-3 self-test ===\r\n");
    kernel_mode_tests();
    ring3_tests();
    out(failures ? "=== SELF-TEST FAILED: " : "=== SELF-TEST PASSED: ");
    num(failures);
    out(" failure(s), ");
    num(passes);
    out(" checks passed ===\r\n");
}

} // namespace posix
