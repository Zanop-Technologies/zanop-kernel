# POSIX layer

First slice of a POSIX compatibility layer for the Zanop kernel. It gives the
kernel Linux-x86-64-style system calls (file I/O, directories, descriptors,
anonymous memory, process basics) so that a C library such as musl can later be
pointed at Zanop OS with only a thin syscall stub.

**Status: kernel-side layer plus the ring-3 foundation.** A program running in
user mode can now make `SYSCALL`s that reach this layer (see
[Ring 3 and SYSCALL](#ring-3-and-syscall)). There is still no ELF loader and no
per-process address spaces, so only small programs loaded by the kernel itself
can run today. See [What is still missing](#what-is-still-missing).

## Layout

| File | Role |
| --- | --- |
| `src/posix/posix_types.hpp` | errno values, flags, `struct stat`, `dirent64`, syscall numbers (Linux x86-64) |
| `src/posix/posix.hpp` | public API and the `Backend` interface |
| `src/posix/posix.cpp` | portable core: per-process fd tables, open-file descriptions, path resolution, syscall semantics |
| `src/posix/kernel_backend.cpp` | binds `Backend` to `FS`, `Terminal`, `Process`, `Signals`, `Memory` |
| `src/posix/libc_shims.cpp` | weak `memcpy`/`memmove`/`memset`/`memcmp`/`strlen` the compiler expects |
| `tests/posix/test_posix.cpp` | host unit tests against an in-memory backend |
| `src/arch/x86_64/usermode.{hpp,cpp,asm}` | GDT/TSS, `SYSCALL` entry, ring-3 entry and exit, CPU exception stubs |
| `src/posix/selftest.cpp`, `src/posix/user_programs.asm` | boot self-test and its ring-3 test programs (only built by `make selftest`) |

The core talks to the kernel only through `posix::Backend`, which is why it can
be tested on a normal machine: `make test-posix`.

Entry point: `Syscall::posix_invoke(nr, a1..a6)` returns the result or `-errno`.
The existing `Syscall::invoke()` is untouched, so current callers keep their
numbering. `posix::init_kernel()` is called from `kernel_main` after `FS::init()`.

Numbers and struct layouts match Linux x86-64 on purpose (`struct stat` is 144
bytes, `getdents64` records, errno values), so musl needs no per-call
translation.

## Implemented system calls

`read` `write` `open` `openat`\* `close` `stat` `lstat` `fstat` `newfstatat`\*
`lseek` `pread64` `pwrite64` `readv` `writev` `access` `dup` `dup2` `fcntl`
`ioctl` (`TCGETS`, `TIOCGWINSZ` on the console) `ftruncate` `getcwd` `chdir`
`rename` (regular files) `mkdir` `rmdir` `unlink` `getdents64` `mmap`
(anonymous, private) `munmap` `mprotect` (accepted, no effect) `brk` (always
`ENOMEM`, so libc falls back to `mmap`) `sched_yield` `getpid` `getppid`
`set_tid_address` `kill` `exit` `exit_group` `uname` `umask` `getuid` family
(always 0) `clock_gettime` (`ENOSYS` until the kernel has a clock).

\* only with `AT_FDCWD` or an absolute path; other directory file descriptors
return `ENOSYS`.

Everything else returns `-ENOSYS`.

## Ring 3 and SYSCALL

`src/arch/x86_64/usermode.{hpp,cpp,asm}` add what the layer needs to be called
from user mode:

* **GDT and TSS.** Segments: `0x08` kernel code, `0x10` kernel data, `0x18` user
  data, `0x20` user code, `0x28` TSS. The layout matches what `SYSRET` expects.
  The TSS gives interrupts that arrive in ring 3 a kernel stack.
* **`SYSCALL` entry.** `EFER.SCE`, `STAR`, `LSTAR` and `SFMASK` are set up in
  `UserMode::init()`, called from `kernel_main` after `posix::init_kernel()`.
  The entry stub follows the Linux convention (number in `rax`, arguments in
  `rdi rsi rdx r10 r8 r9`) and preserves every register except `rax`, `rcx` and
  `r11`, as musl expects. It then calls `posix::syscall()`. Interrupts stay
  masked during a call.
* **CPU exceptions 0 to 31.** The IDT used to handle only keyboard, mouse and
  network interrupts, so any fault was a triple fault and a silent reset. Now a
  fault in a user program ends that program and `UserMode::run()` returns
  `-(1000 + vector)` (for example `-1014` for a page fault, `-1013` for #GP). A
  fault in the kernel prints the vector, error code, `rip` and `cr2` on the
  serial port and halts.
* **One user region.** `UserMode::prepare_region()` marks a single 2 MiB page at
  physical (= virtual) `0x0C000000` accessible from ring 3 by setting the user
  bit on the entries leading to it (`Paging::make_user_accessible`). Everything
  else stays supervisor-only, including the higher-half kernel mapping, which
  has its own PML4 entry without the user bit.
* **Running a program.** `UserMode::run(entry, stack_top, pid)` enters ring 3
  and returns the program's `exit()` status, or the fault code above. The
  program's descriptors are tracked under the virtual process id `pid` and are
  closed when it ends. The caller of `run()` keeps its own descriptors and
  working directory.
* **Pointer checks.** While a program runs, `posix::syscall()` refuses (with
  `EFAULT`) any pointer, buffer or string that is not entirely inside the user
  region, so a user program cannot make the kernel read or write kernel memory
  for it. Kernel callers are unaffected and may pass any non-null pointer.

Limits of this first step:

| Limit | Detail |
| --- | --- |
| One program at a time | One user region, one saved kernel context. `run()` is not re-entrant and there is no scheduler. |
| No isolation between runs | All programs share the same identity-mapped region, which is cleared by the caller between runs. |
| Fixed address | The region is at 192 MiB. The kernel heap is at 324 MiB (see below), so any machine that boots the kernel has memory there. |
| `mmap` from user mode | Returns `ENOSYS`: there are no per-process page tables to map more memory into. |
| `exit()` | Ends the program; the status is the low 8 bits. |
| No signals, threads, `fork`/`exec` | Not implemented. |

**Memory requirement of the kernel.** The page tables map the 2 MiB pages from
physical `0x10000000` at virtual `0x440000000`, and the kernel heap starts at
`0x444400000`, which is physical `0x14400000` (324 MiB). The kernel therefore
needs more than about 325 MiB of RAM. With less (for example QEMU `-m 256`) the
heap writes are rejected. Use `-m 1024` when running it.

## Limits inherited from the current kernel

These come from the existing filesystem, heap and process table rather than
from the POSIX layer, and are the first things to lift as the kernel grows.

| Limit | Value | Source |
| --- | --- | --- |
| Absolute path length | 47 characters | `NAME_SIZE` in `fs.cpp` |
| Files and directories | 64 | `DISK_FILE_COUNT` |
| File size | below the 100 KiB heap, and read/write/truncate are O(file size) | `FS` has no partial I/O; the adapter buffers the whole file |
| Writes without a mounted disk | `EROFS` | `FS::write_file` needs a disk |
| Processes with fd tables | 16 | `Process::MAX_PROCESSES` |
| Descriptors per process | 32 | `MAX_FDS` |
| Open file descriptions (system wide) | 61 | `MAX_OFDS` minus the console |
| Permissions, ownership, timestamps | none: files report `0644`, directories `0755`, uid 0, time 0 | `FS` stores none |
| Inode numbers | synthetic (hash of the path) | no inodes in `FS` |
| `/system/*` | cannot be removed or renamed (`EACCES`) | `FS` rule |

Other deliberate behavior:

* `exit` marks the process exited through `Process::terminate()` and frees its
  descriptors. Processes are still cooperative functions, so the entry function
  must return afterwards.
* A process's descriptor table is created on its first POSIX call. When the
  16-entry table is full, entries of processes that no longer exist are
  reclaimed; if every process is alive the call fails with `EAGAIN`.
* `mmap` results are returned as a signed `i64`; addresses in the upper half
  would look like errors, so keep the user heap below that range.
* Pointer arguments are only checked for null (`user_ok()` in `posix.cpp`).
  That function is the single place to add real user-range validation when ring
  3 exists.

## Verification

* `make test-posix` builds the core with `-Wall -Wextra -Werror` and
  AddressSanitizer/UBSan and runs 359 checks (ABI layout, every syscall above,
  error paths, descriptor sharing, table exhaustion, process isolation, and the
  user-pointer checks for every kind of argument).
* `make` builds and links the whole kernel.
* `make selftest` builds a kernel that runs a self-test at boot and prints the
  results on the serial port, and `make run-selftest` also creates a blank
  64 MB disk and starts QEMU with it (`-m 1024`, serial on the terminal). It
  needs QEMU, GRUB and `xorriso`. The test has two parts:
  1. kernel-mode calls to `posix::syscall()` against a freshly formatted disk;
  2. small programs, assembled from `src/posix/user_programs.asm`, run in ring 3:
     one opens a file, writes to it, closes it and checks that registers survive
     `SYSCALL` and that `getpid()` returns its virtual pid; one passes kernel
     pointers and must get `EFAULT`; one reads kernel memory and must page
     fault; one executes `hlt` and must raise #GP. Afterwards the kernel checks
     that it is back in ring 0 with interrupts enabled and still works.

  A passing run ends with `=== SELF-TEST PASSED: 0 failure(s), 36 checks passed ===`.
  Run `make clean` afterwards to return to a normal build.
* A normal build was also booted in QEMU to confirm the desktop still starts
  and keyboard and mouse interrupts still work with the new GDT.
* **Not verified:** real hardware, and anything beyond the small test programs
  (there is no ELF loader yet).

## What is still missing

In the order a port of larger software such as GIMP would need them:

1. Per-process page tables and a preemptive scheduler, so several programs can
   run, `mmap` can work from user mode, and programs are isolated from each
   other. (Ring 3 and the `SYSCALL` entry now exist.)
2. An ELF loader, then static executables.
3. A C library: port musl with a syscall stub that matches this ABI. It also
   needs `arch_prctl` (thread-local storage), signals (`rt_sigaction`,
   `rt_sigprocmask`), `futex` and `clone` (threads), and `getrandom`.
4. A timer and wall clock for `clock_gettime`, `nanosleep`, and file times.
5. `fork`/`execve`/`wait4` or `posix_spawn`, `pipe`, `poll`/`select`, sockets
   (the network stack currently handles ARP only).
6. A filesystem with partial reads and writes, larger files and permissions.
7. Dynamic linking, then a GDK backend for the Zanop display, then GLib, babl,
   GEGL and GTK, and finally the GIMP port itself.
