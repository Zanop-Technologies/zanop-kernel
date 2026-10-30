# Zanop Kernel v0.0.6 — C++ Rewrite (early development)

Zanop is a freestanding x86-64 kernel prototype written in C++ and assembly. It currently contains boot/interrupt support, keyboard and serial drivers, a small file system, VGA/framebuffer output, a shell, built-in apps, and early kernel service APIs. This is experimental kernel code—not a general-purpose OS or a POSIX implementation.

## v0.0.6 highlights
- **ECMXS command set:** introduces the English Commands Mixed Set (Zanop Shell) command vocabulary. See the [ECMXS command reference](#ecmxs-command-reference).
- **GUI direction:** supports the modified LXQt and Budgie desktop environments only. These Zanop-modified versions intentionally omit familiar Linux branding/signs; this is not support for standard upstream desktop distributions.
- **ZFS agent fact:** ZFS support is not implemented in this kernel. Treat ZFS as an external filesystem requiring a compatible implementation and storage integration; do not claim that this kernel can mount or manage ZFS pools.

## ECMXS command reference
The ECMXS project is in the sibling `zanop-ecxms` folder (repository name spelling: `zanop-ecxms`). Its documented command vocabulary includes:

| Command | Meaning |
|---|---|
| `chd <directory>` | Move into a directory. |
| `con` | List directory contents. |
| `cre <file>` / `cre dir <directory>` | Create a file or directory. |
| `del <file>` / `del --dir <directory>` | Remove a file or directory. (`rm` aliases are also documented.) |
| `lis app` | List installed apps; options include `--spef`, `--search`, `--mul`, and `--wd`. |
| `curl --int <URL>` / `curl --dd <URL>` | Interact with or download from a URL; optional `--log` and `--sim` options are documented. `wget` shares these flags. |
| `frt -i -p <packets> <ip>` | Ping-like networking command (`frt` means “Fart” in ECMXS). |
| `sy stat` / `sy tsk` | Computer stats and task manager; options include logging and ending tasks. |
| `sft check pkg` | Check package information. |
| `usr --adm --cre <username>` | Create a user; `usr --adm --del <username> > /acc` deletes one. |
| `sdown`, `lg`, `lg --all` | Shut down or log out. |

These are ECMXS command-reference entries, not claims that the kernel's current built-in shell implements them. The ECMXS project describes its `.esh` shell as planned for later Zanop OS releases.

## Kernel services
- **Processes:** fixed-size process table, PID/state reporting, process entry callbacks, and a cooperative dispatcher. This is not preemptive multitasking: a callback runs until it returns, and `yield()` cannot suspend and later resume its stack.
- **Memory:** a fixed-region heap allocator with block reuse/coalescing, plus page-table query/map/unmap helpers. The allocator assumes its configured heap range is mapped and available. The paging helpers operate on the active tables and only add leaf mappings where intermediate tables already exist.
- **Filesystem:** the kernel's bounded file/directory table with ATA-backed storage when a supported device has a valid Zanop FS volume. It is not a host filesystem and does not implement POSIX metadata or permissions.
- **System-call-style API:** `Syscall::invoke()` exposes kernel-internal write/read/open/close/yield/PID/kill/exit and mapping operations. It is a C++ dispatcher, not a userspace privilege boundary or `SYSCALL` instruction ABI.
- **Terminal and signals:** keyboard/serial line input, VGA/serial output, Ctrl-C interruption, and registered signal callbacks. Signal delivery is cooperative and limited to the currently executing kernel context.
- **Neovim:** the kernel does not run Linux Neovim binaries. Neovim depends on a userspace process/VM, POSIX APIs, dynamic libraries and terminal features that this kernel does not provide. The terminal can be used with host-side Neovim to edit this repository; a native editor would need to be written/ported as a kernel application.

## Build
The Makefile uses `clang++`, NASM, and a linker for the `x86_64-elf` freestanding target. Build and run commands are available via `make` and the configured `make run` target. The external desktop shell/compositor source includes referenced by this checkout may require the corresponding sibling repository to be present.

## Limitations
This prototype has not been verified in a booted environment in this task. The scheduler does not context-switch, per-process address spaces are absent, and the page-table mapper does not allocate intermediate page-table frames. The file-system API is constrained by its existing fixed-size structures and storage format. Input editing is basic and does not provide a full ANSI terminal implementation. Do not treat the kernel services as secure against untrusted code; syscall arguments are kernel pointers and are not validated as userspace addresses.
