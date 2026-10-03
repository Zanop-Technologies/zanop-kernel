#include "pic.hpp"

namespace {

inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
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
uint8_t pic1_mask = 0xF9;
uint8_t pic2_mask = 0xFF;

void init() {
    (void)inb(PIC1_DATA);
    (void)inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, 32);
    io_wait();
    outb(PIC2_DATA, 40);
    io_wait();
    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();
    pic1_mask = 0xF9;
    pic2_mask = 0xFF;
    outb(PIC1_DATA, pic1_mask);
    outb(PIC2_DATA, pic2_mask);
}

void send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_COMMAND, 0x20);
    outb(PIC1_COMMAND, 0x20);
}

void enable_irq(uint8_t irq) {
    if (irq < 8) {
        pic1_mask &= static_cast<uint8_t>(~(1u << irq));
        outb(PIC1_DATA, pic1_mask);
        return;
    }
    if (irq < 16) {
        pic2_mask &= static_cast<uint8_t>(~(1u << (irq - 8)));
        pic1_mask &= static_cast<uint8_t>(~(1u << 2));
        outb(PIC2_DATA, pic2_mask);
        outb(PIC1_DATA, pic1_mask);
    }
}

void disable_irq(uint8_t irq) {
    if (irq < 8) {
        pic1_mask |= static_cast<uint8_t>(1u << irq);
        outb(PIC1_DATA, pic1_mask);
        return;
    }
    if (irq < 16) {
        pic2_mask |= static_cast<uint8_t>(1u << (irq - 8));
        outb(PIC2_DATA, pic2_mask);
    }
}

} // namespace PIC
} // namespace Arch