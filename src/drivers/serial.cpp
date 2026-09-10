#include "serial.hpp"

namespace {

constexpr uint16_t SERIAL_PORT = 0x3F8;

bool is_transmit_empty() {
    return __builtin_ia32_inb(SERIAL_PORT + 5) & 0x20;
}

void write_char(char a) {
    while (!is_transmit_empty());
    __builtin_ia32_outb(SERIAL_PORT, a);
}

} // anonymous namespace

namespace Drivers {
namespace Serial {

void init() {
    __builtin_ia32_outb(SERIAL_PORT + 1, 0x00);
    __builtin_ia32_outb(SERIAL_PORT + 3, 0x80);
    __builtin_ia32_outb(SERIAL_PORT, 0x03);
    __builtin_ia32_outb(SERIAL_PORT + 1, 0x00);
    __builtin_ia32_outb(SERIAL_PORT + 3, 0x03);
    __builtin_ia32_outb(SERIAL_PORT + 2, 0xC7);
    __builtin_ia32_outb(SERIAL_PORT + 4, 0x0B);
}

void write(char c) {
    write_char(c);
}

void write_string(const char* str) {
    while (*str) {
        write(*str++);
    }
}

bool is_transmit_empty() {
    return ::is_transmit_empty();
}

} // namespace Serial
} // namespace Drivers