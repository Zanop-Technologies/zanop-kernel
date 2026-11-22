#include "e1000.hpp"
#include "../arch/x86_64/pic.hpp"
#include "../arch/x86_64/idt.hpp"
#include "../memory/paging.hpp"
#include "serial.hpp"

namespace Drivers {
namespace E1000 {

// ---------------------------------------------------------------------------
// PCI configuration space access (ports 0xCF8 / 0xCFC)
// ---------------------------------------------------------------------------
namespace {

constexpr std::uint16_t PCI_ADDRESS_PORT = 0xCF8;
constexpr std::uint16_t PCI_DATA_PORT    = 0xCFC;

constexpr std::uint8_t  PCI_VENDOR_ID    = 0x00;
constexpr std::uint8_t  PCI_DEVICE_ID    = 0x02;
constexpr std::uint8_t  PCI_COMMAND      = 0x04;
constexpr std::uint8_t  PCI_BAR0         = 0x10;
constexpr std::uint8_t  PCI_INTERRUPT_LINE = 0x3C;

constexpr std::uint16_t E1000_VENDOR_ID  = 0x8086; // Intel
constexpr std::uint16_t E1000_DEVICE_ID  = 0x100E; // 82540EM (QEMU default)

std::uint32_t pci_read(std::uint8_t bus, std::uint8_t slot,
                       std::uint8_t func, std::uint8_t offset) {
    const std::uint32_t address =
        (static_cast<std::uint32_t>(bus) << 16) |
        (static_cast<std::uint32_t>(slot) << 11) |
        (static_cast<std::uint32_t>(func) << 8) |
        (offset & 0xFC) | 0x80000000u;
    __asm__ volatile("outl %0, %1" : : "a"(address), "Nd"(PCI_ADDRESS_PORT));
    std::uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(PCI_DATA_PORT));
    return value;
}

void pci_write(std::uint8_t bus, std::uint8_t slot, std::uint8_t func,
               std::uint8_t offset, std::uint32_t value) {
    const std::uint32_t address =
        (static_cast<std::uint32_t>(bus) << 16) |
        (static_cast<std::uint32_t>(slot) << 11) |
        (static_cast<std::uint32_t>(func) << 8) |
        (offset & 0xFC) | 0x80000000u;
    __asm__ volatile("outl %0, %1" : : "a"(address), "Nd"(PCI_ADDRESS_PORT));
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(PCI_DATA_PORT));
}

// ---------------------------------------------------------------------------
// Register map (datasheet chapter 3, memory-mapped register set)
// ---------------------------------------------------------------------------
constexpr std::uint32_t REG_CTRL     = 0x0000;  // Device Control
constexpr std::uint32_t REG_STATUS   = 0x0008;  // Device Status
constexpr std::uint32_t REG_EECD     = 0x0010;  // EEPROM Control
constexpr std::uint32_t REG_EERD     = 0x0014;  // EEPROM Read
constexpr std::uint32_t REG_CTRL_EXT = 0x0018;  // Device Control Extension
constexpr std::uint32_t REG_IMS      = 0x00D0;  // Interrupt Mask Set
constexpr std::uint32_t REG_ICS      = 0x00C8;  // Interrupt Cause Set
constexpr std::uint32_t REG_ICR      = 0x00C0;  // Interrupt Cause Read
constexpr std::uint32_t REG_ITR      = 0x00C4;  // Interrupt Throttling Rate
constexpr std::uint32_t REG_RCTL     = 0x0100;  // Receive Control
constexpr std::uint32_t REG_TCTL     = 0x0400;  // Transmit Control
constexpr std::uint32_t REG_RDBAL    = 0x2800;  // RX Descriptor Base Low
constexpr std::uint32_t REG_RDBAH    = 0x2804;  // RX Descriptor Base High
constexpr std::uint32_t REG_RDLEN    = 0x2808;  // RX Descriptor Length
constexpr std::uint32_t REG_RDH      = 0x2810;  // RX Descriptor Head
constexpr std::uint32_t REG_RDT      = 0x2818;  // RX Descriptor Tail
constexpr std::uint32_t REG_RXDMT0   = 0x2840;  // RX Interrupt Delay Timer
constexpr std::uint32_t REG_TDBAL    = 0x3800;  // TX Descriptor Base Low
constexpr std::uint32_t REG_TDBAH    = 0x3804;  // TX Descriptor Base High
constexpr std::uint32_t REG_TDLEN    = 0x3808;  // TX Descriptor Length
constexpr std::uint32_t REG_TDH      = 0x3810;  // TX Descriptor Head
constexpr std::uint32_t REG_TDT      = 0x3818;  // TX Descriptor Tail
constexpr std::uint32_t REG_TIPG     = 0x0410;  // TX Inter-Packet Gap
constexpr std::uint32_t REG_RAL      = 0x5400;  // Receive Address Low
constexpr std::uint32_t REG_RAH      = 0x5404;  // Receive Address High
constexpr std::uint32_t REG_MTA      = 0x5200;  // Multicast Table Array
constexpr std::uint32_t REG_CRCERRS  = 0x4000;  // CRC Error Count

// CTRL bits
constexpr std::uint32_t CTRL_SLU     = 1 << 6;  // Set Link Up
constexpr std::uint32_t CTRL_FRCSPD  = 1 << 11; // Force Speed
constexpr std::uint32_t CTRL_FRCDPX  = 1 << 12; // Force Duplex
constexpr std::uint32_t CTRL_LRST    = 1 << 3;  // Link Reset

// STATUS bits
constexpr std::uint32_t STATUS_LU    = 1 << 1;  // Link Up

// RCTL bits
constexpr std::uint32_t RCTL_EN      = 1 << 1;  // RX Enable
constexpr std::uint32_t RCTL_SBP     = 1 << 2;  // Store Bad Packets
constexpr std::uint32_t RCTL_UPE     = 1 << 3;  // Unicast Promiscuous
constexpr std::uint32_t RCTL_MPE     = 1 << 4;  // Multicast Promiscuous
constexpr std::uint32_t RCTL_LPE     = 1 << 5;  // Long Packet (jumbo) enable
constexpr std::uint32_t RCTL_BSIZE_2048 = 0 << 16; // 2048-byte buffers
constexpr std::uint32_t RCTL_BSEX_9K = 1 << 25;    // BSEX: jumbo buffer size
constexpr std::uint32_t RCTL_SECRC   = 1 << 26; // Strip Ethernet CRC

// TCTL bits
constexpr std::uint32_t TCTL_EN      = 1 << 1;  // TX Enable
constexpr std::uint32_t TCTL_PSP     = 1 << 3;  // Pad Short Packets
constexpr std::uint32_t TCTL_CT_SHIFT  = 4;      // Collision Threshold
constexpr std::uint32_t TCTL_COLD_SHIFT = 12;    // Collision Distance
constexpr std::uint32_t TCTL_SWXOFF   = 1 << 22; // Software XOFF
constexpr std::uint32_t TCTL_RTLC     = 1 << 24; // Re-transmit on Late Collision

// Interrupt bits (ICR/IMS)
constexpr std::uint32_t INT_TXDW     = 1 << 0;  // TX Descriptor Written Back
constexpr std::uint32_t INT_RX       = 1 << 2;  // RX Interrupt
constexpr std::uint32_t INT_LSC      = 1 << 2;  // NOTE: RX is bit 2, LSC is bit 2
constexpr std::uint32_t INT_RXSEQ    = 1 << 3;  // RX Sequence Error
constexpr std::uint32_t INT_LSC_BIT  = 1 << 2;  // Link Status Change
constexpr std::uint32_t INT_RXDT     = 1 << 7;  // RX Timer Interrupt
constexpr std::uint32_t INT_RXO      = 1 << 6;  // RX Overrun
constexpr std::uint32_t INT_MASK     = INT_TXDW | INT_RX | INT_RXSEQ | INT_LSC_BIT | INT_RXDT | INT_RXO;

// Descriptor ring sizes (datasheet: multiples of 8, up to 64K)
constexpr std::uint16_t RX_RING_SIZE = 64;
constexpr std::uint16_t TX_RING_SIZE = 64;
// Jumbo-capable receive buffer (9216 bytes per factsheet, rounded up).
constexpr std::uint16_t RX_BUF_SIZE  = 9216;
constexpr std::uint16_t TX_BUF_SIZE  = 2048;

// Legacy transmit descriptor format (datasheet chapter 7.2.1).
struct __attribute__((packed)) TxDescriptor {
    std::uint64_t buffer_address;
    std::uint16_t length;
    std::uint8_t  cso;
    std::uint8_t  cmd;
    std::uint8_t  status;
    std::uint8_t  csum;
    std::uint16_t mss;
};
constexpr std::uint8_t TX_CMD_EOP  = 1 << 0;
constexpr std::uint8_t TX_CMD_IFCS = 1 << 1; // Insert FCS (checksum offload)
constexpr std::uint8_t TX_CMD_IC   = 1 << 2; // Insert Checksum
constexpr std::uint8_t TX_CMD_RS   = 1 << 3; // Report Status
constexpr std::uint8_t TX_CMD_DEXT = 1 << 5; // Descriptor extension
constexpr std::uint8_t TX_STATUS_DD = 1 << 0; // Descriptor Done

// Legacy receive descriptor format (datasheet chapter 7.1.1).
struct __attribute__((packed)) RxDescriptor {
    std::uint64_t buffer_address;
    std::uint16_t length;
    std::uint16_t checksum;
    std::uint8_t  status;
    std::uint8_t  errors;
    std::uint16_t special;
};
constexpr std::uint8_t RX_STATUS_DD = 1 << 0; // Descriptor Done
constexpr std::uint8_t RX_STATUS_EOP = 1 << 1; // End of Packet

// ---------------------------------------------------------------------------
// MMIO helpers
// ---------------------------------------------------------------------------
volatile std::uint8_t* mmio = nullptr;

inline std::uint32_t read_reg(std::uint32_t offset) {
    return *reinterpret_cast<volatile std::uint32_t*>(mmio + offset);
}

inline void write_reg(std::uint32_t offset, std::uint32_t value) {
    *reinterpret_cast<volatile std::uint32_t*>(mmio + offset) = value;
}

// ---------------------------------------------------------------------------
// EEPROM access (datasheet chapter 5.4) -- reads the MAC address
// ---------------------------------------------------------------------------
bool eeprom_read(std::uint8_t word, std::uint16_t& value) {
    // The EERD register exists on the 8254x family. If the EEPROM is not
    // present, the controller falls back to a hardcoded MAC in RAL/RAH.
    constexpr std::uint32_t EERD_START = 1 << 0;
    constexpr std::uint32_t EERD_DONE  = 1 << 4;
    write_reg(REG_EERD, (static_cast<std::uint32_t>(word) << 8) | EERD_START);
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (read_reg(REG_EERD) & EERD_DONE) {
            value = static_cast<std::uint16_t>(read_reg(REG_EERD) >> 16);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Device state
// ---------------------------------------------------------------------------
struct Device {
    std::uint8_t bus;
    std::uint8_t slot;
    std::uint8_t func;
    std::uint8_t irq_line;
    bool present;
    bool initialized;
};

Device device = {};

Stats device_stats = {};

// Descriptor rings and buffers live in the kernel heap (identity-mapped
// by Memory::Paging::init, so physical == virtual for our allocations).
// Buffers are page-aligned so that map_page-friendly DMA addresses are
// guaranteed to be valid.
struct __attribute__((aligned(4096))) RxRing {
    RxDescriptor descriptors[RX_RING_SIZE];
    std::uint8_t  buffers[RX_RING_SIZE][RX_BUF_SIZE];
};
struct __attribute__((aligned(4096))) TxRing {
    TxDescriptor descriptors[TX_RING_SIZE];
    std::uint8_t  buffers[TX_RING_SIZE][TX_BUF_SIZE];
};

RxRing rx_ring;
TxRing tx_ring;
std::uint16_t rx_head = 0; // hardware index (next packet the NIC writes)
std::uint16_t rx_tail = 0; // software index (next descriptor we own)
std::uint16_t tx_tail = 0;
std::uint16_t tx_head = 0;

std::uint16_t rx_next_poll = 0;
bool rx_polling = false;

} // namespace

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
void init() {
    device.present = false;
    device.initialized = false;

    // Scan PCI bus 0, slots 0..31, function 0 for the E1000 device.
    for (std::uint8_t slot = 0; slot < 32; ++slot) {
        const std::uint32_t vendor = pci_read(0, slot, 0, PCI_VENDOR_ID) & 0xFFFF;
        if (vendor == 0xFFFF) continue;
        const std::uint32_t device_id = pci_read(0, slot, 0, PCI_DEVICE_ID) & 0xFFFF;
        if (vendor != E1000_VENDOR_ID || device_id != E1000_DEVICE_ID) continue;

        device.bus = 0;
        device.slot = slot;
        device.func = 0;
        device.present = true;
        break;
    }
    if (!device.present) {
        serial::write_string("e1000: no Intel 82540EM (8086:100E) found\n");
        return;
    }

    // Enable bus mastering and memory space so the device can DMA.
    const std::uint32_t command = pci_read(device.bus, device.slot, device.func, PCI_COMMAND);
    pci_write(device.bus, device.slot, device.func, PCI_COMMAND,
              command | 0x02 | 0x04 | 0x400); // MEM | BUSMASTER | PARITY

    // BAR0 is the 128KB MMIO window (datasheet chapter 3.2).
    const std::uint32_t bar0 = pci_read(device.bus, device.slot, device.func, PCI_BAR0);
    const std::uintptr_t physical = bar0 & 0xFFFFF000u;
    const std::uintptr_t virtual_address = physical; // identity mapped
    mmio = reinterpret_cast<volatile std::uint8_t*>(virtual_address);

    device.irq_line = static_cast<std::uint8_t>(
        pci_read(device.bus, device.slot, device.func, PCI_INTERRUPT_LINE) & 0xFF);

    // Software reset (datasheet chapter 4.3.3): CTRL.LRST, then clear.
    write_reg(REG_CTRL, read_reg(REG_CTRL) | CTRL_LRST);
    write_reg(REG_CTRL, 0);

    // Disable interrupts while we configure the rings.
    write_reg(REG_IMS, 0);

    // Interrupt throttling: rate limit to ~1024 interrupts/sec (datasheet
    // chapter 4.4.2, InterruptThrottleRate analog).
    write_reg(REG_ITR, 1024);

    // -- MAC address: read from EEPROM, fall back to RAL/RAH --
    std::uint16_t mac_words[3] = {0, 0, 0};
    bool have_mac = true;
    for (std::uint8_t word = 0; word < 3; ++word) {
        if (!eeprom_read(word, mac_words[word])) {
            have_mac = false;
            break;
        }
    }
    if (!have_mac) {
        // Fall back to the Receive Address registers (datasheet 4.6.4).
        const std::uint32_t ral = read_reg(REG_RAL);
        const std::uint32_t rah = read_reg(REG_RAH);
        mac_words[0] = static_cast<std::uint16_t>(ral);
        mac_words[1] = static_cast<std::uint16_t>(ral >> 16);
        mac_words[2] = static_cast<std::uint16_t>(rah);
    }
    for (int i = 0; i < 6; ++i) {
        device_stats.mac[i] = static_cast<std::uint8_t>(mac_words[i / 2] >> ((i % 2) * 8));
    }

    // Accept the multicast/broadcast packets we need (all MTA bits set).
    for (std::uint32_t i = 0; i < 0x80; i += 4) {
        write_reg(REG_MTA + i, 0xFFFFFFFFu);
    }

    // -- Receive ring --
    for (std::uint16_t i = 0; i < RX_RING_SIZE; ++i) {
        rx_ring.descriptors[i].buffer_address = reinterpret_cast<std::uint64_t>(&rx_ring.buffers[i][0]);
        rx_ring.descriptors[i].status = 0;
        rx_ring.descriptors[i].length = 0;
    }
    write_reg(REG_RDBAL, static_cast<std::uint32_t>(reinterpret_cast<std::uint64_t>(&rx_ring.descriptors[0]) & 0xFFFFFFFFu));
    write_reg(REG_RDBAH, static_cast<std::uint32_t>(reinterpret_cast<std::uint64_t>(&rx_ring.descriptors[0]) >> 32));
    write_reg(REG_RDLEN, RX_RING_SIZE * sizeof(RxDescriptor));
    write_reg(REG_RDH, 0);
    write_reg(REG_RDT, RX_RING_SIZE - 1);
    rx_head = 0;
    rx_tail = RX_RING_SIZE - 1;
    rx_next_poll = 0;

    // Receive control: enable, 2048-byte buffers (jumbo ready via BSEX),
    // store bad packets off, strip CRC, broadcast + unicast + multicast.
    write_reg(REG_RCTL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_LPE |
                        RCTL_BSIZE_2048 | RCTL_BSEX_9K | RCTL_SECRC);

    // RX interrupt delay timer (throttling, datasheet 4.4.2).
    write_reg(REG_RXDMT0, 0);

    // -- Transmit ring --
    for (std::uint16_t i = 0; i < TX_RING_SIZE; ++i) {
        tx_ring.descriptors[i].buffer_address = reinterpret_cast<std::uint64_t>(&tx_ring.buffers[i][0]);
        tx_ring.descriptors[i].status = 0;
        tx_ring.descriptors[i].cmd = 0;
        tx_ring.descriptors[i].length = 0;
    }
    write_reg(REG_TDBAL, static_cast<std::uint32_t>(reinterpret_cast<std::uint64_t>(&tx_ring.descriptors[0]) & 0xFFFFFFFFu));
    write_reg(REG_TDBAH, static_cast<std::uint32_t>(reinterpret_cast<std::uint64_t>(&tx_ring.descriptors[0]) >> 32));
    write_reg(REG_TDLEN, TX_RING_SIZE * sizeof(TxDescriptor));
    write_reg(REG_TDH, 0);
    write_reg(REG_TDT, 0);
    tx_head = 0;
    tx_tail = 0;

    // Transmit control: enable, pad short packets, collision threshold
    // 15, collision distance 64 (datasheet 4.4.4).
    write_reg(REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_RTLC |
                        (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT));
    // Inter-packet gap (datasheet 4.4.5): IPGR2=8, IPGR1=10, IPGT=10.
    write_reg(REG_TIPG, (10 << 0) | (10 << 10) | (8 << 20));

    // Bring the link up (datasheet 4.3.3): CTRL.SLU, force speed+duplex.
    std::uint32_t ctrl = read_reg(REG_CTRL);
    ctrl |= CTRL_SLU | CTRL_FRCSPD | CTRL_FRCDPX;
    write_reg(REG_CTRL, ctrl);

    // Enable the interrupts we care about (datasheet 4.4.2).
    write_reg(REG_IMS, INT_MASK);

    // Unmask the PIC IRQ line (E1000 uses IRQ 11 by default).
    Arch::PIC::enable_irq(device.irq_line);

    device.initialized = true;
    serial::write_string("e1000: Intel 82540EM (8086:100E) at PCI 0:");
    serial::write(static_cast<char>('0' + device.slot));
    serial::write_string(", IRQ ");
    serial::write(static_cast<char>('0' + device.irq_line));
    serial::write_string(", initialized\n");
}

bool is_present() {
    return device.present && device.initialized;
}

const Stats& stats() {
    return device_stats;
}

// ---------------------------------------------------------------------------
// Transmit
// ---------------------------------------------------------------------------
bool send_packet(const std::uint8_t* data, std::uint16_t length) {
    if (!is_present() || data == nullptr) return false;
    if (length == 0 || length > TX_BUF_SIZE) return false;

    // Recycle descriptors that the hardware has finished with.
    while (tx_head != tx_tail) {
        const std::uint16_t done = static_cast<std::uint16_t>((tx_head + 1) % TX_RING_SIZE);
        if (tx_ring.descriptors[done].status & TX_STATUS_DD) {
            tx_head = done;
            device_stats.tx_packets++;
        } else {
            break;
        }
    }

    const std::uint16_t next = static_cast<std::uint16_t>((tx_tail + 1) % TX_RING_SIZE);
    if (next == tx_head) {
        device_stats.tx_dropped++;
        return false; // ring full
    }

    TxDescriptor& descriptor = tx_ring.descriptors[tx_tail];
    std::uint8_t* buffer = &tx_ring.buffers[tx_tail][0];
    for (std::uint16_t i = 0; i < length; ++i) {
        buffer[i] = data[i];
    }
    descriptor.buffer_address = reinterpret_cast<std::uint64_t>(buffer);
    descriptor.length = length;
    descriptor.cso = 0;
    descriptor.cmd = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    descriptor.status = 0;

    // Hand the descriptor to the hardware (datasheet 4.4.4).
    write_reg(REG_TDT, next);
    tx_tail = next;
    return true;
}

// ---------------------------------------------------------------------------
// Receive (NAPI-style polling)
// ---------------------------------------------------------------------------
std::uint16_t poll_receive(std::uint8_t* buffer, std::uint16_t buffer_size) {
    if (!is_present() || buffer == nullptr || buffer_size == 0) return 0;

    RxDescriptor& descriptor = rx_ring.descriptors[rx_next_poll];
    if (!(descriptor.status & RX_STATUS_DD)) return 0;

    const std::uint16_t length = descriptor.length;
    if (length > buffer_size) {
        device_stats.rx_dropped++;
    } else {
        const std::uint8_t* source = &rx_ring.buffers[rx_next_poll][0];
        for (std::uint16_t i = 0; i < length; ++i) {
            buffer[i] = source[i];
        }
        device_stats.rx_packets++;
    }
    descriptor.status = 0;

    // Re-arm the descriptor: advance the hardware tail.
    rx_next_poll = static_cast<std::uint16_t>((rx_next_poll + 1) % RX_RING_SIZE);
    rx_tail = static_cast<std::uint16_t>((rx_tail + 1) % RX_RING_SIZE);
    write_reg(REG_RDT, rx_tail);
    return length;
}

// ---------------------------------------------------------------------------
// Interrupt handler
// ---------------------------------------------------------------------------
extern "C" void e1000_interrupt_handler() {
    if (!is_present()) return;

    const std::uint32_t cause = read_reg(REG_ICR);
    if (cause == 0) return;

    if (cause & INT_LSC_BIT) {
        // Link status change: update the link state.
        device_stats.link_up = (read_reg(REG_STATUS) & STATUS_LU) != 0;
    }
    if (cause & (INT_RX | INT_RXDT)) {
        // NAPI-style: the interrupt only signals that packets are pending;
        // the actual polling happens in poll_receive().
        rx_polling = true;
    }
    if (cause & INT_RXO) {
        device_stats.rx_dropped++;
    }
    if (cause & INT_TXDW) {
        // TX done; the descriptor is recycled in send_packet().
    }

    // Acknowledge all raised causes (writing the value back clears them).
    write_reg(REG_ICR, cause);
}

} // namespace E1000
} // namespace Drivers
