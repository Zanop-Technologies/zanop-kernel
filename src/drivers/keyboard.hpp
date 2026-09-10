#ifndef DRIVERS_KEYBOARD_HPP
#define DRIVERS_KEYBOARD_HPP

#include <cstdint>
#include <optional>

namespace Drivers {
namespace Keyboard {

void init();
void handle_interrupt();
uint8_t read_scancode();
std::optional<uint8_t> try_read_scancode();
std::optional<char> read_char();

} // namespace Keyboard
} // namespace Drivers

#endif