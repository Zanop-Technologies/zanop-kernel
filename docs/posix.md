# POSIX layer

First slice of a POSIX compatibility layer for the Zanop kernel. It gives the
kernel Linux-x86-64-style system calls (file I/O, directories, descriptors,
anonymous memory, process basics) so that a C library such as musl can later be
pointed at Zanop OS with only a thin syscall stub.

**Status: kernel-side half only.** The kernel still has no userspace (no ring 3,
no ELF loader, no per-process address spaces), so today the layer is reachable
only from code running inside the kernel, through `Syscall::posix_invoke()`.
See [What is still missing](#what-is-still-missing).

## Layout

| File | Role |
| --- | --- |
| `src/posix/posix_types.hpp` | errno values, flags, `struct stat`, `dirent64`, syscall numbers (Linux x86-64) |
| `src/posix/posix.hpp` | public API and the `Backend` interface |
| `src/posix/posix.cpp` | portable core: per-process fd tables, open-file descriptions, path resolution, syscall semantics |
| `src/posix/kernel_backend.cpp` | binds `Backend` to `FS`, `Terminal`, `Process`, `Signals`, `Memory` |
| `src/posix/libc_shims.cpp` | weak `memcpy`/`memmove`/`memset`/`memcmp`/`strlen` the compiler expects |
| `tests/posix/test_posix.cpp` | host unit tests against an in-memory backend |

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
  AddressSanitizer/UBSan and runs 324 checks (ABI layout, every syscall above,
  error paths, descriptor sharing, table exhaustion, process isolation).
* `make` builds and links the whole kernel with the layer included. The Makefile
  now passes `-Isrc`; without it the sibling `zanop-desktop-os` sources fail on
  `#include "types.hpp"`.
* **Not verified:** booting the kernel or exercising `kernel_backend.cpp` on
  real hardware or an emulator. That file only runs inside the kernel, and the
  README states the kernel has not been verified in a booted environment.

## What is still missing

In the order a port of larger software such as GIMP would need them:

1. Ring 3, a `SYSCALL`/`SYSRET` entry that calls `posix::syscall`, per-process
   page tables, and a preemptive scheduler.
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
