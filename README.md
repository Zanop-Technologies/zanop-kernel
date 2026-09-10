# Zanop Kernel — C++ Rewrite (Alpha v0.0.5, in progress)

This is the **first skeleton** of the C++ rewrite, not a full port of
everything the Rust kernel had. The goal here is to prove the whole
toolchain (Clang freestanding target → NASM → ld.lld → Limine → QEMU)
actually works end to end with a minimal kernel, before porting the
larger subsystems over.

## Why C++ + Clang, and why this approach specifically

The Rust build hit nearly every one of its real, hard-to-debug errors
in the nightly-only `-Zbuild-std`/`-Zjson-target-spec` machinery
itself — the pointer-width type, the ABI field name, the soft-float
feature location all changed between nightly snapshots during actual
debugging. Clang's `x86_64-unknown-none` target is a **first-class,
stable** freestanding target — no custom target JSON, no nightly
compiler, no `build-std` step compiling `core`/`alloc` from source
every time.

`boot.asm` and `linker.ld` are carried over **unchanged in logic**
from the corrected versions that actually booted successfully
tonight (Multiboot2 header, `gdt64` kept in low memory in `.boot`
rather than `.rodata`, page tables/stack in `.bss.boot` rather than
the default higher-half `.bss`). Assembly-level concerns don't care
what language calls into them, so none of that debugging was wasted.

## What's ported so far

- `boot.asm` / `linker.ld` — unchanged logic from the working Rust build
- A minimal VGA text-mode writer (`drivers/vga.hpp`/`.cpp`)
- `kernel_main` — clears the screen, prints a boot message, halts

## What's deliberately NOT ported yet

Everything else from the Rust kernel — on purpose, so this skeleton
stays small enough to actually verify works before building on it:

- Serial, keyboard, RTC drivers
- PIC/IDT interrupt handling
- The in-memory filesystem
- The shell (`chd`/`cre`/`con`/`info`/`app`)
- The four built-in apps (Calculator, Notes, Clock, Snake)

These get ported in follow-up passes once this skeleton is confirmed
booting for real.

## Building

Requires: `clang++`, `nasm`, `ld.lld`, `xorriso` — all already
available in the proot Ubuntu environment from tonight's session.
Also assumes `../limine` (the Limine bootloader repo, already cloned
and built tonight) sits as a sibling folder to this one.

```bash
make
make run
```

`make run` builds a bootable ISO via Limine (the same recipe that
successfully booted the Rust kernel tonight) and launches it in QEMU
with `-display curses`, so you'll see the VGA text output directly in
your terminal.

## Known limitations (honest, not hedging)

- **Not yet tested** — same as every kernel build tonight, this was
  written carefully following the same patterns that worked, but
  hasn't been run yet. Given how many small things went wrong in the
  Rust build's toolchain layer specifically (not the code layer), the
  actual code/logic here has a reasonable chance of working on the
  first or second try — but "reasonable chance" isn't "confirmed."
- No memory allocator yet, so nothing beyond simple stack-based logic
  will work (no dynamic containers, no heap).
- No interrupt handling yet, so no keyboard input is possible at all
  until PIC/IDT gets ported.

## Next steps

Run `make && make run`, confirm the boot message appears, then port
subsystems one at a time (driver layer first, then filesystem/shell,
then apps) — same order they were originally built in, each one
build-tested before moving to the next, rather than porting
everything at once.
