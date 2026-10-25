# Zanop Kernel — C++ Rewrite (early development)

Zanop is a freestanding x86-64 kernel prototype written in C++ and assembly. It currently contains boot/interrupt support, keyboard and serial drivers, a small file system, VGA/framebuffer output, a shell, built-in apps, and early kernel service APIs. This is experimental kernel code—not a general-purpose OS or a POSIX implementation.

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
