# Zanop Kernel v0.0.9 — C++ Rewrite (early development)

Zanop is a freestanding x86-64 kernel prototype written in C++ and assembly. It currently contains boot/interrupt support, keyboard and serial drivers, a small file system, VGA/framebuffer output, a shell with built-in apps, a first POSIX compatibility layer, and ring-3 `SYSCALL` support.

## v0.0.9 highlights

- **Ring-3 and POSIX foundation:** a first kernel-side POSIX compatibility layer exposes Linux x86-64-style system-call numbers and ABI layouts, and GDT/TSS plus `SYSCALL`/ring-3 support lets kernel-loaded test programs run in user mode.
- **User-mode protection:** CPU exceptions in ring 3 return fault codes instead of silently triple-faulting; pointer arguments must remain inside the dedicated user region, and kernel memory stays supervisor-only.
- **POSIX system calls:** file descriptors, file/directory operations, I/O, memory calls, process identity, signals/kill, exit, `uname`, `getuid` family, and several other Linux-style calls are available through `Syscall::posix_invoke()`.
- **User management MVP:** a bounded 32-user table provides user creation/removal, lookup by ID or name, roles, username updates, and hashed-password updates through generation-checked handles.
- **Boot & interrupts:** boot/interrupt support, keyboard and serial drivers, VGA/framebuffer output, and a shell with built-in apps.
- **Kernel services:** fixed-size process table with a cooperative dispatcher, a fixed-region heap allocator with block reuse/coalescing, page-table query/map/unmap helpers, and a `Syscall::invoke()` C++ dispatcher.
- **Filesystem:** bounded file/directory table with ATA-backed storage when a supported device has a valid Zanop FS volume; not POSIX.
- **Terminal & signals:** keyboard/serial line input, Ctrl-C interruption, and registered signal callbacks (cooperative, current context only).
- **ECMXS command set:** documents the English Commands Mixed Set (Zanop Shell) vocabulary. See the [ECMXS command reference](#ecmxs-command-reference).
- **GUI direction:** supports modified LXQt and Budgie desktop environments only, without standard upstream Linux branding.
- **Not supported:** ZFS is not implemented (treat as an external filesystem); Linux Neovim binaries do not run (no ELF loader, libc, dynamic linking, or complete userspace environment).

## ECMXS command reference

The ECMXS project is in the sibling `zanop-ecxms` folder (repository name spelling: `zanop-ecxms`). Its documented command vocabulary includes:

| Command                                | Meaning                                                                                                               |
| -------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `chd <directory>`                      | Move into a directory.                                                                                                |
| `con`                                  | List directory contents.                                                                                              |
| `cre <file>` / `cre dir <directory>`   | Create a file or directory.                                                                                           |
| `del <file>` / `del --dir <directory>` | Remove a file or directory. (`rm` aliases are also documented.)                                                       |
| `lis app`                              | List installed apps; options include `--spef`, `--search`, `--mul`, and `--wd`.                                       |
| `curl --int <URL>` / `curl --dd <URL>` | Interact with or download from a URL; optional `--log` and `--sim` options are documented. `wget` shares these flags. |
| `frt -i -p <packets> <ip>`             | Ping-like networking command (`frt` means “Fart” in ECMXS).                                                           |
| `sy stat` / `sy tsk`                   | Computer stats and task manager; options include logging and ending tasks.                                            |
| `sft check pkg`                        | Check package information.                                                                                            |
| `usr --adm --cre <username>`           | Create a user; `usr --adm --del <username> > /acc` deletes one.                                                       |
| `sdown`, `lg`, `lg --all`              | Shut down or log out.                                                                                                 |

These are ECMXS command-reference entries, not claims that the kernel's current built-in shell implements them. The ECMXS project describes its `.esh` shell as planned for later Zanop OS releases.

## Kernel services

- **Processes:** fixed-size process table, PID/state reporting, process entry callbacks, and a cooperative dispatcher. This is not preemptive multitasking: a callback runs until it returns, and `yield()` only returns control after the callback cooperates.
- **Memory:** a fixed-region heap allocator with block reuse/coalescing, plus page-table query/map/unmap helpers. The allocator assumes its configured heap range is mapped and available. The paging helpers do not implement full per-process virtual-memory management.
- **Filesystem:** the kernel's bounded file/directory table with ATA-backed storage when a supported device has a valid Zanop FS volume. It is not a host filesystem and does not implement POSIX metadata or full POSIX file semantics.
- **System-call-style APIs:** `Syscall::invoke()` remains the existing C++ kernel-internal dispatcher. `Syscall::posix_invoke()` dispatches Linux-style POSIX calls and returns results or `-errno`; the two APIs keep separate numbering.
- **Ring 3:** GDT/TSS setup, `SYSCALL` entry, ring-3 program entry, CPU exception stubs, and a dedicated 2 MiB user region are implemented. Programs run one at a time and share that region in this first implementation.
- **Terminal and signals:** keyboard/serial line input, VGA/serial output, Ctrl-C interruption, and registered signal callbacks. Signal delivery is cooperative and limited to the currently executing kernel context.
- **User management:** `src/auth/users_manager.cpp` provides a 32-slot in-memory MVP. Users have numeric IDs, names, password hashes, roles, active/retired state, and generation-checked handles. It is not yet a persistent account system or a complete security boundary.
- **Neovim:** the kernel does not run Linux Neovim binaries. Neovim depends on userspace, POSIX APIs, dynamic libraries, an ELF loader, and terminal features that remain incomplete or absent.

## POSIX and ring-3 status

The first POSIX slice is documented in [`docs/posix.md`](docs/posix.md). It includes Linux x86-64 system-call numbers and layouts, descriptor and open-file tables, file/directory operations, anonymous private mappings, process basics, and ring-3 `SYSCALL` entry. Host unit tests cover the core implementation, and `make selftest` boots a kernel containing small ring-3 test programs.

Important limits include: no ELF loader, no per-process page tables, no preemptive scheduler, no isolation between ring-3 runs, no `fork`/`exec`, threads, pipes, sockets, C library, dynamic linking, or complete POSIX clock implementation. `mmap` is limited to anonymous private mappings in kernel mode.

## Build

The Makefile uses `clang++`, NASM, and a linker for the `x86_64-elf` freestanding target. Build and run commands are available via `make` and the configured `make run` target. The external desktop shell and GUI components are not included in the kernel repository.

Run `make test-posix` for host POSIX unit tests, `make selftest` to build the ring-3/POSIX boot self-test, or `make run-selftest` to build and launch that test with QEMU. Ring-3 testing uses a sufficiently large QEMU RAM configuration, typically `-m 1024`.

## Note for developers

<!-- DEV COMMENT: When anything in this README changes (new features, removed features, changed behaviour, updated build steps), bump the version number in the title and update this file in the same commit. -->

## Limitations

This prototype has not been verified on real hardware. The scheduler remains cooperative, per-process address spaces are absent, ring-3 programs share one fixed 2 MiB region, and the page-table mapper does not allocate new intermediate page-table structures. The POSIX layer and user manager are early implementations and must not be treated as a complete security or account boundary.
