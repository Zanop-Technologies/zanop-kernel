#ifndef DRIVERS_E1000_HPP
#define DRIVERS_E1000_HPP

// Intel E1000 (82540EM / 8254x family) Gigabit Ethernet driver.
//
// Register offsets, bit definitions and the descriptor ring layout follow
// the Intel 82540EM/82545EM/82546EM Gigabit Ethernet Controller Datasheet
// (Intel document 317542) and the factsheet in docs/supported_drivers.md.
//
// Supported features (factsheet):
//   - Gigabit Ethernet via DMA with circular descriptor rings
//   - Hardware checksum offload (TX packet checksum insertion)
//   - Interrupt throttling (RXDMT0 timer) and receive polling
//   - Link status (STATUS register) / Wake-on-LAN capable controller
//   - Jumbo frames (up to 9216 bytes) via RCTL.BSIZE/BSEX
//   - QEMU/VMware/VirtualBox 8086:100E emulation supported

#include "../types.hpp"

namespace Drivers {
namespace E1000 {

struct Stats {
    std::uint64_t rx_packets;
    std::uint64_t tx_packets;
    std::uint64_t rx_dropped;
    std::uint64_t tx_dropped;
    bool link_up;
    std::uint8_t  mac[6];
};

void init();
bool is_present();
const Stats& stats();

// Transmit one packet. Returns false if the device is absent or the
// transmit ring is full (no room after the descriptor is recycled).
bool send_packet(const std::uint8_t* data, std::uint16_t length);

// Poll one received packet (NAPI-style polling). Returns the number of
// bytes copied into `buffer` (0 if no packet is available).
std::uint16_t poll_receive(std::uint8_t* buffer, std::uint16_t buffer_size);

// Interrupt handler (called from the IRQ11 stub, vector 43).
extern "C" void e1000_interrupt_handler();

} // namespace E1000
} // namespace Drivers

#endif
