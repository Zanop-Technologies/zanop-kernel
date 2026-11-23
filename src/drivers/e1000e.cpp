#include "e1000.hpp"
#include "e1000e.hpp"
#include "serial.hpp"
#include "arch/x86_64/pic.hpp"
#include "arch/x86_64/idt.hpp"
#include "memory/paging.hpp"

namespace Drivers {
namespace E1000E {

// Same PCI constants as base E1000, but device ID 0x10e8 for 82574L
constexpr std::uint16_t E1000E_VENDOR_ID  = 0x8086; // Intel
constexpr std::uint16_t E1000E_DEVICE_ID  = 0x10e8; // 82574L

// Use base class implementation; only override probing
using Drivers::E1000::Device;

static bool probe_device(std::uint8_t bus, std::uint8_t slot, std::uint8_t func)
{
    auto vendor = Drivers::E1000::pci_read(bus, slot, func, Drivers::E1000::PCI_VENDOR_ID);
    auto device = Drivers::E1000::pci_read(bus, slot, func, Drivers::E1000::PCI_DEVICE_ID);
    if((vendor & 0xFFFF) != E1000E_VENDOR_ID || (device & 0xFFFF) != E1000E_DEVICE_ID)
        return false;
    return true;
}

// The rest of the initialization follows the same pattern as e1000.cpp

static void init_device(std::uint8_t bus, std::uint8_t slot, std::uint8_t func)
{
    // Enable bus mastering
    Drivers::E1000::pci_write(bus, slot, func, Drivers::E1000::PCI_COMMAND, 0x0005);
    // Map BAR0
    Drivers::E1000::mmio = reinterpret_cast<volatile std::uint8_t*>(Drivers::E1000::pci_ioremap_bar(bus, slot, func, 0));
    // Initialize registers: enable Rx/Tx, reset link
    Drivers::E1000::write_reg(Drivers::E1000::REG_RCTL, Drivers::E1000::RCTL_EN | Drivers::E1000::RCTL_LPE);
    Drivers::E1000::write_reg(Drivers::E1000::REG_TCTL, Drivers::E1000::TCTL_EN | Drivers::E1000::TCTL_PSP);
    // Use generic EV18 init: set MAC via EEPROM or default
    std::uint16_t mac1, mac2, mac3; // placeholder
    Drivers::E1000::eeprom_read(0, mac1);
    Drivers::E1000::eeprom_read(1, mac2);
    Drivers::E1000::eeprom_read(2, mac3);
    // Store MAC into RAL/RAH etc.
}

// Interrupt handler registration would use existing PIC/IDT helpers

} // namespace E1000E
} // namespace Drivers
