#ifndef PANIC_HPP
#define PANIC_HPP

#include <cstdint>

struct PanicInfo {
    const char* message;
    const char* file;
    uint32_t line;
};

void panic_handler(const PanicInfo& info) __attribute__((noreturn));

#endif