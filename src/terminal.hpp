#ifndef DRIVERS_TERMINAL_HPP
#define DRIVERS_TERMINAL_HPP
#include "types.hpp"
namespace Terminal {
void init();
void write(const char* text);
void write_bytes(const uint8_t* bytes, size_t length);
void put(char character);
size_t read_line(char* buffer, size_t capacity);
size_t read(char* buffer, size_t capacity);
bool try_read(char& character);
void request_interrupt();
}
#endif
