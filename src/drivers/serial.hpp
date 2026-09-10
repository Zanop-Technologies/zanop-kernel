#ifndef DRIVERS_SERIAL_HPP
#define DRIVERS_SERIAL_HPP

#include <cstdint>

namespace Drivers {
namespace Serial {

void init();
void write(char c);
void write_string(const char* str);
bool is_transmit_empty();

} // namespace Serial
} // namespace Drivers

#endif