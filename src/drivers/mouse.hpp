#pragma once

#include "../types.hpp"

namespace Drivers {
namespace Mouse {

struct Event {
    int delta_x;
    int delta_y;
    uint8_t buttons;
};

bool init();
bool is_available();
uint8_t init_error();
void handle_interrupt();
std::optional<Event> try_read_event();

} // namespace Mouse
} // namespace Drivers