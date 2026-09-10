# FIXES.md — Zanop Kernel Common Bugs & Fixes

A running reference of real bugs hit during this project, why they
happened, and how they were fixed — so the same mistakes don't cost
another multi-hour debugging session. Organized by category. Add to
this file whenever a new non-obvious bug gets tracked down.

---

## Fabricated compiler builtins (C++ build)

**Symptom:** "use of undeclared identifier" or "unknown builtin
function" at compile time.

**Root cause:** Code using `__builtin_ia32_inb`, `__builtin_ia32_outb`,
or `__builtin_ia32_hlt` — none of these exist in GCC or Clang. The
real `__builtin_ia32_*` family is exclusively SIMD/vector intrinsics
(MMX/SSE/AVX, e.g. `__builtin_ia32_kunpckdi`), not port I/O or
privileged instructions. Verified by search — every real-world port
I/O implementation (glibc, musl, Android Bionic, several hobby OS
projects) uses inline assembly, never a compiler builtin, for this.

**Fix:** Always use real inline asm:
```cpp
inline void outb(std::uint16_t port, std::uint8_t val) {
    asm volatile("out %0, %1" : : "a"(val), "Nd"(port));
}
inline std::uint8_t inb(std::uint16_t port) {
    std::uint8_t val;
    asm volatile("in %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
// halt:
asm volatile("hlt");
```

**Lesson:** If a `__builtin_*` name looks suspiciously convenient and
you haven't seen it in real kernel code before, verify it exists
before trusting it — this exact mistake showed up three times in a
row (`inb`, `outb`, `hlt`) from the same source, suggesting a pattern
of confidently-invented builtins rather than one-off typos.

---

## Namespace / API mismatches between header and caller

**Symptom:** Multiple files individually compile fine, but linking
(or even compiling the caller) fails with undeclared-identifier
errors — or worse, silently doesn't fail because an unrelated
overload matches, hiding the real problem.

**Root cause:** A file gets rewritten (renamed namespace, changed
function signatures, restructured API) without re-checking every
caller. Found repeatedly: `serial::` rewritten to `Drivers::Serial::`
while other files still expected lowercase `serial::`; `vga::writer`
(object + methods) vs. `VGA::init()/println()/print()` (free
functions) — two completely different API shapes for the same
subsystem existing in different files simultaneously; `Memory::init()`
called but only `Memory::Allocator::init()` (one namespace level
deeper) actually declared.

**Fix:** Whenever changing a namespace name, a function signature, or
which functions exist at all, grep the whole tree for every call site
before considering the change done:
```bash
grep -rn "OldNamespace::" src/
grep -rn "old_function_name" src/
```

**Lesson:** These mismatches are the single most common and costly
bug class in this project so far — more time was lost to
header/caller drift than to any actual logic bug. When multiple
files get edited across separate passes (especially by different
tools/sessions), treat a full-repo consistency grep as a required
step, not an optional cleanup.

---

## Missing global `operator new`/`operator delete`

**Symptom:** Code compiles cleanly, but linking fails with an
undefined-reference error for `operator new(unsigned long)` or
similar — only shows up once something in the codebase actually uses
plain `new`, which might be far away from the allocator file itself.

**Root cause:** `-nostdlib` (required for a freestanding kernel)
means no default `operator new`/`delete` are linked in. A custom
allocator (`kmalloc`/bump allocator/etc.) doesn't automatically wire
itself up to `new`/`delete` — that requires explicitly overloading
the global operators.

**Fix:**
```cpp
void* operator new(std::size_t size) { return Memory::allocate(size); }
void* operator new[](std::size_t size) { return Memory::allocate(size); }
void operator delete(void* ptr) noexcept { Memory::deallocate(ptr); }
void operator delete[](void* ptr) noexcept { Memory::deallocate(ptr); }
```

**Lesson:** Check for these overloads any time a memory-allocation
header is reviewed — their absence doesn't show up until link time,
and only if something happens to use plain `new` rather than a
custom allocator function directly.

---

## Relying on libc functions under `-nostdlib`

**Symptom:** Compiles fine (headers like `<cstring>`/`<cstdlib>` just
declare prototypes), fails at link time with undefined references to
`memcpy`, `strcmp`, `strtol`, etc.

**Root cause:** `-nostdlib` excludes libc, not just libstdc++. Every
libc function used anywhere in the kernel (`memcpy`, `memset`,
`strlen`, `strcmp`, `strchr`, `strtol`, `strtok`, ...) needs its own
implementation somewhere in the kernel, since none will be linked in
automatically.

**Fix:** Write minimal versions of only the specific functions
actually used, and prefer avoiding the more complex ones (`strtok`,
`strtol`) by writing simple manual parsers instead of reimplementing
their full semantics. See `src/kstd/libc_shims.cpp` for the current
set (`memcpy`, `memset`, `memmove`, `strlen`, `strcmp`, `strncmp`,
`strchr`).

**Lesson:** Before using any `<cstring>`/`<cstdlib>` function in new
code, check whether it's already in `libc_shims.cpp` — if not, either
add it there or avoid it.

---

## Unmapped higher-half kernel addresses (page fault on boot)

**Symptom:** Kernel builds and links successfully, boots via
Limine/QEMU, but nothing ever appears on screen — no crash message,
no output, just silence (a triple fault resets the VM before any
diagnostic can print).

**Root cause:** `linker.ld` places kernel code/data at a higher-half
virtual address (`KERNEL_VMA = 0xFFFFFFFF80000000`), but `boot.asm`'s
`setup_page_tables` only built ONE L4/L3 entry — identity-mapping the
low 1GB (virtual = physical). The higher-half virtual range was never
mapped at all, so the instant execution jumps to higher-half code
after the long-mode transition, the CPU page-faults into nothing.

**Fix:** Map a second L3 table into L4 index 511 (computed precisely
for `KERNEL_VMA`, not guessed), pointing its relevant L3 entry at the
SAME L2 table the low mapping already uses — since the kernel is
physically loaded within that same low 1GB range, this correctly
resolves higher-half virtual addresses to the right physical memory:
```nasm
mov eax, page_table_l2
or eax, 0b11
mov [page_table_l3_high + 510 * 8], eax   ; L3 index 510 for this VMA

mov eax, page_table_l3_high
or eax, 0b11
mov [page_table_l4 + 511 * 8], eax        ; L4 index 511 for this VMA
```
(Indices computed via `(KERNEL_VMA >> 39) & 0x1FF` etc. — always
compute these for your actual `KERNEL_VMA`, don't copy indices from
another project without checking.)

**Lesson:** A higher-half kernel design needs BOTH a low mapping (for
the pre-paging bootstrap code) AND a high mapping (for the kernel
itself) from the very first page table setup — this is easy to miss
since the low mapping alone is enough to get past `enable_paging`
without an immediate fault, making the bug invisible until the CPU
actually jumps into higher-half code.

---

## Missing `SECTIONS { }` wrapper in linker.ld

**Symptom:** `rust-lld`/`ld.lld` error: `unknown directive: .boot :`

**Root cause:** Section definitions (`.boot :`, `.text ALIGN(4K) :`,
etc.) were written as bare top-level statements instead of being
wrapped inside a `SECTIONS { ... }` block, and `ENTRY(_start)` /
`KERNEL_VMA = ...` were missing entirely.

**Fix:** Always structure `linker.ld` as:
```ld
ENTRY(_start)
KERNEL_VMA = 0x...;
SECTIONS
{
    . = 1M;
    .boot : { ... }
    ...
}
```

**Lesson:** When copying a linker script between projects or
rewriting it from memory, verify the outer structure exists — it's
easy to paste just the section bodies and forget the wrapper.

---

## Duplicate linker.ld files with only one actually used

**Symptom:** Editing `linker.ld` and rebuilding doesn't change the
error at all — same relocation-out-of-range error persists no matter
what the file says.

**Root cause:** Two copies of `linker.ld` existed (one at repo root,
one under `src/arch/x86_64/`) from an earlier build-script iteration.
`build.rs`/the Makefile referenced the root one via a relative
`-Tlinker.ld`, but all the debugging edits were being made to the
`src/arch/x86_64/` copy.

**Fix:**
```bash
find . -name "linker.ld"   # check for duplicates FIRST
cp src/arch/x86_64/linker.ld linker.ld  # sync them, or fix the build reference
```

**Lesson:** When an edit doesn't change build behavior at all, check
for duplicate/stale copies of the file before assuming the edit
itself is wrong — this wastes a full rebuild cycle if missed.

---

## QEMU's `-kernel` flag can't load 64-bit ELF or Multiboot2

**Symptom:** `qemu-system-x86_64: Cannot load x86-64 image, give a
32bit one.` or `Error loading uncompressed kernel without PVH ELF
Note.`

**Root cause:** QEMU's built-in `-kernel` loader only understands
32-bit Multiboot v1 or the Linux boot protocol — it cannot load a
kernel that's 64-bit ELF (even if the actual code only runs 64-bit
logic after its own internal long-mode transition), and it doesn't
understand Multiboot2 headers at all.

**Fix:** Use a real bootloader (GRUB or Limine) to build a bootable
ISO instead of `-kernel`:
```bash
qemu-system-x86_64 -cdrom target/zanop-os.iso -no-reboot -no-shutdown -display curses
```

**Lesson:** `-kernel` is only useful for the simplest possible 32-bit
test kernels — anything doing a real long-mode transition needs an
actual bootloader in the loop from the start, not as a fallback.

---

## Limine boots into graphics mode instead of VGA text mode

**Symptom:** Boot succeeds (Limine's own "NNNN x NNNN Graphic mode"
message appears), but the kernel's own text output never shows,
even though the kernel almost certainly ran.

**Root cause:** Limine auto-selects a graphics framebuffer mode by
default. The kernel's VGA writer only knows how to write to the
legacy text-mode buffer at `0xB8000` — in graphics mode that address
doesn't mean the same thing, so text output is effectively invisible
even if it's technically still being "written" somewhere.

**Fix:** Force text mode in `limine.conf`:
```
video_mode: text
```
(Remember to rebuild the ISO after editing this — the running `.iso`
file is a snapshot, not a live reference to the config file.)

**Lesson:** After changing any config that affects the boot image,
always rebuild the ISO before re-testing — re-running the same stale
`.iso` after an edit is a very easy false negative.

---

## General checklist before trusting a "fix" without testing

Given how many of the above were subtle, silent, or only surfaced at
an unrelated later step, treat every fix as provisional until an
actual `make`/`cargo build` + boot test confirms it — code that
"looks right" has been wrong in this project more than once.
