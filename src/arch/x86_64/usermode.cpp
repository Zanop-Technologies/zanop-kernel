// Ring-3 support: GDT/TSS, SYSCALL setup, system-call and exception dispatch.
// The assembly half is usermode.asm.
#include "usermode.hpp"
#include "../../drivers/serial.hpp"
#include "../../memory/paging.hpp"
#include "../../posix/posix.hpp"

extern "C" {
void zanop_syscall_entry();
long long zanop_enter_user(uint64_t entry, uint64_t user_rsp);
[[noreturn]] void zanop_user_return(long long status);
extern const uint64_t zanop_exception_stubs[32];
uint64_t zanop_syscall_stack_top;
}

namespace Arch {
namespace UserMode {

namespace {

constexpr uint16_t SEL_KERNEL_CODE = 0x08;
constexpr uint16_t SEL_TSS = 0x28;
constexpr uint32_t MSR_EFER = 0xC0000080;
constexpr uint32_t MSR_STAR = 0xC0000081;
constexpr uint32_t MSR_LSTAR = 0xC0000082;
constexpr uint32_t MSR_SFMASK = 0xC0000084;
// Cleared on SYSCALL entry: TF, IF, DF, AC.
constexpr uint64_t SFMASK_VALUE = 0x100 | 0x200 | 0x400 | 0x40000;

struct __attribute__((packed)) Tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

struct __attribute__((packed)) GdtPointer {
    uint16_t limit;
    uint64_t base;
};

// Matches the register pushes in zanop_syscall_entry (last push = lowest).
struct SyscallFrame {
    uint64_t r9, r8, r10, rdx, rsi, rdi, rax;
    uint64_t r11, rcx, user_rsp;
};

// Matches the pushes in exception_common plus the CPU-pushed frame.
struct ExceptionFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

alignas(16) uint64_t gdt[7];
Tss tss;
alignas(16) uint8_t kernel_stack[32768];
bool running = false;

void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(static_cast<uint32_t>(value)),
                     "d"(static_cast<uint32_t>(value >> 32)));
}

uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}

void print_hex(uint64_t value) {
    serial::write_string("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned digit = static_cast<unsigned>((value >> shift) & 0xF);
        serial::write(static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10));
    }
}

void print_labeled(const char* label, uint64_t value) {
    serial::write_string(label);
    print_hex(value);
    serial::write(' ');
}

} // namespace

void init() {
    __asm__ volatile("cli" ::: "memory");

    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFull; // 0x08 kernel code, 64-bit
    gdt[2] = 0x00CF92000000FFFFull; // 0x10 kernel data
    gdt[3] = 0x00CFF2000000FFFFull; // 0x18 user data   (DPL 3)
    gdt[4] = 0x00AFFA000000FFFFull; // 0x20 user code   (DPL 3, 64-bit)

    const uint64_t tss_base = reinterpret_cast<uint64_t>(&tss);
    const uint64_t tss_limit = sizeof(Tss) - 1;
    gdt[5] = (tss_limit & 0xFFFF) | ((tss_base & 0xFFFFFF) << 16) | (0x89ull << 40) |
             (((tss_limit >> 16) & 0xF) << 48) | (((tss_base >> 24) & 0xFF) << 56);
    gdt[6] = tss_base >> 32;

    zanop_syscall_stack_top = reinterpret_cast<uint64_t>(kernel_stack) + sizeof(kernel_stack);
    tss.rsp[0] = zanop_syscall_stack_top; // stack for interrupts taken in ring 3
    tss.iomap_base = sizeof(Tss);

    const GdtPointer pointer{static_cast<uint16_t>(sizeof(gdt) - 1),
                             reinterpret_cast<uint64_t>(gdt)};
    __asm__ volatile("lgdt %0" : : "m"(pointer));
    // Reload CS with a far return, then the data segments.
    __asm__ volatile("pushq %0\n\t"
                     "leaq 1f(%%rip), %%rax\n\t"
                     "pushq %%rax\n\t"
                     "lretq\n"
                     "1:"
                     : : "i"(static_cast<uint64_t>(SEL_KERNEL_CODE)) : "rax", "memory");
    __asm__ volatile("movw $0x10, %%ax\n\t"
                     "movw %%ax, %%ds\n\t"
                     "movw %%ax, %%es\n\t"
                     "movw %%ax, %%ss" : : : "ax", "memory");
    __asm__ volatile("ltr %0" : : "r"(SEL_TSS));

    write_msr(MSR_EFER, read_msr(MSR_EFER) | 1); // SCE: enable SYSCALL/SYSRET
    write_msr(MSR_STAR, (0x10ull << 48) | (0x08ull << 32));
    write_msr(MSR_LSTAR, reinterpret_cast<uint64_t>(&zanop_syscall_entry));
    write_msr(MSR_SFMASK, SFMASK_VALUE);

    __asm__ volatile("sti" ::: "memory");
}

bool prepare_region() {
    return Memory::Paging::make_user_accessible(static_cast<uintptr_t>(USER_BASE));
}

long long run(uint64_t entry, uint64_t stack_top, uint32_t pid) {
    posix::enter_user_context(pid, USER_BASE, USER_BASE + USER_SIZE);
    running = true;
    const long long status = zanop_enter_user(entry, stack_top);
    running = false;
    posix::leave_user_context();
    return status;
}

bool active() { return running; }

} // namespace UserMode
} // namespace Arch

using Arch::UserMode::ExceptionFrame;
using Arch::UserMode::SyscallFrame;

// Called from zanop_syscall_entry on the kernel stack, interrupts masked.
extern "C" long long zanop_syscall_dispatch(SyscallFrame* frame) {
    if (frame->rax == posix::nr::EXIT || frame->rax == posix::nr::EXIT_GROUP) {
        zanop_user_return(static_cast<long long>(frame->rdi & 0xFF)); // POSIX keeps 8 bits
    }
    return posix::syscall(frame->rax, frame->rdi, frame->rsi, frame->rdx,
                          frame->r10, frame->r8, frame->r9);
}

// Called from exception_common. A fault in a user program ends that program;
// a fault in the kernel is reported on the serial port and halts the machine.
extern "C" void zanop_exception_dispatch(ExceptionFrame* frame) {
    uint64_t cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    const bool from_user = (frame->cs & 3) == 3;

    serial::write_string(from_user ? "\r\nuser exception: " : "\r\nKERNEL EXCEPTION: ");
    Arch::UserMode::print_labeled("vector=", frame->vector);
    Arch::UserMode::print_labeled("error=", frame->error);
    Arch::UserMode::print_labeled("rip=", frame->rip);
    Arch::UserMode::print_labeled("cr2=", cr2);
    serial::write_string("\r\n");

    if (from_user && Arch::UserMode::active()) {
        zanop_user_return(-(Arch::UserMode::FAULT_BASE + static_cast<long long>(frame->vector)));
    }
    // Returning makes exception_common halt the CPU.
}
