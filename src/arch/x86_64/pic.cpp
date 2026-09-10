#include "pic.hpp"

namespace {

inline void outb(uint16_t port, uint8_t val) {
    __builtin_ia32_outb(port, val);
}

inline uint8_t inb(uint16_t port) {
    return __builtin_ia32_inb(port);
}

inline void io_wait() {
    outb(0x80, 0);
}

} // anonymous namespace

namespace Arch {
namespace PIC {

constexpr uint16_t PIC1_COMMAND = 0x20;
constexpr uint16_t PIC1_DATA = 0x21;
constexpr uint16_t PIC2_COMMAND = 0xA0;
constexpr uint16_t PIC2_DATA = 0xA1;

constexpr uint8_t ICW1_INIT = 0x10;
constexpr uint8_t ICW1_ICW4 = 0x01;
constexpr uint8_t ICW4_8086 = 0x01;

constexpr uint8_t PIC1_OFFSET = 32;
constexpr uint8_t PIC2_OFFSET = 40;

void init() {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);
    (void)mask1; (void)mask2;
    
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    
    outb(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);
    io_wait();
    
    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();
    
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    
    outb(PIC1_DATA, 0xFD);  // Mask all except IRQ1 (keyboard)
    outb(PIC2_DATA, 0xFF);  // Mask all
}

void send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}

} // namespace PIC
} // namespace Arch