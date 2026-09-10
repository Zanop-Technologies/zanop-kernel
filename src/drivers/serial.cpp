#include "serial.hpp"
#include <cstdint>

namespace serial {
namespace {

constexpr std::uint16_t SERIAL_PORT = 0x3F8;

inline void outb(std::uint16_t port, std::uint8_t val) {
    asm volatile("out %0, %1" : : "a"(val), "Nd"(port));
}
inline std::uint8_t inb(std::uint16_t port) {
    std::uint8_t val;
    asm volatile("in %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

bool is_transmit_empty() {
    return (inb(SERIAL_PORT + 5) & 0x20) != 0;
}

void write_char(char c) {
    while (!is_transmit_empty()) {}
    outb(SERIAL_PORT, static_cast<std::uint8_t>(c));
}

} // anonymous namespace

void init() {
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x03);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);
}

void write_string(const char* s) {
    while (*s) write_char(*s++);
}

} // namespace serial