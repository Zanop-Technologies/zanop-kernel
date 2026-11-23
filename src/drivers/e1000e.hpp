#pragma once
// Header for Intel 82574L (E1000e) Ethernet driver extensions
// Includes minimal declarations needed by src/drivers/e1000e.cpp

#include "e1000.hpp"

namespace Drivers {
namespace E1000E {

// expose init_device and probe_device for external use if needed
bool probe_device(std::uint8_t bus, std::uint8_t slot, std::uint8_t func);
void init_device(std::uint8_t bus, std::uint8_t slot, std::uint8_t func);

} // namespace E1000E
} // namespace Drivers
