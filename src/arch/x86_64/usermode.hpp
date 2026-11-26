#ifndef ARCH_X86_64_USERMODE_HPP
#define ARCH_X86_64_USERMODE_HPP

#include "../../types.hpp"

namespace Arch {
namespace UserMode {

// The one region user programs may touch: a single 2 MiB page at physical
// (= virtual) 192 MiB, below the kernel heap at 324 MiB. It is the only memory
// marked user-accessible; all other memory stays supervisor-only.
constexpr uint64_t USER_BASE = 0x0C000000ull;
constexpr uint64_t USER_SIZE = 0x200000ull;

// run() returns -(FAULT_BASE + vector) when the program caused a CPU
// exception, e.g. -1014 for a page fault and -1013 for a general protection
// fault. A normal exit(n) returns n.
constexpr long long FAULT_BASE = 1000;

// Installs the GDT with user segments and a TSS, and enables the SYSCALL
// instruction. Call once after Arch::init() and Memory::init().
void init();

// Marks the user region accessible from ring 3.
bool prepare_region();

// Runs the program loaded at `entry` in ring 3 on `stack_top` and returns its
// exit status. System calls from it go to posix::syscall() with pointer
// arguments checked against the user region. `pid` is the virtual process id
// its file descriptors are tracked under.
long long run(uint64_t entry, uint64_t stack_top, uint32_t pid);

// True while run() is executing a user program.
bool active();

} // namespace UserMode
} // namespace Arch

#endif
