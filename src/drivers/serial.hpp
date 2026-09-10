#ifndef DRIVERS_SERIAL_HPP
#define DRIVERS_SERIAL_HPP

#include <cstdint>

namespace serial {

void init();
void write(char c);
void write_string(const char* str);
bool is_transmit_empty();

} // namespace serial

#endif