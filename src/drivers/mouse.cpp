#include "mouse.hpp"
#include "../arch/x86_64/pic.hpp"

namespace {

constexpr uint16_t DATA_PORT = 0x60;
constexpr uint16_t STATUS_PORT = 0x64;
constexpr size_t QUEUE_SIZE = 32;

Drivers::Mouse::Event events[QUEUE_SIZE];
volatile uint8_t queue_head = 0;
volatile uint8_t queue_tail = 0;
uint8_t packet[3];
uint8_t packet_index = 0;
bool available = false;
uint8_t error_stage = 0;

inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

bool wait_input_empty() {
    for (uint32_t count = 0; count < 1000000; ++count) {
        if ((inb(STATUS_PORT) & 0x02) == 0) return true;
        __asm__ volatile("pause");
    }
    return false;
}

bool wait_output_full() {
    for (uint32_t count = 0; count < 1000000; ++count) {
        if ((inb(STATUS_PORT) & 0x01) != 0) return true;
        __asm__ volatile("pause");
    }
    return false;
}

bool controller_command(uint8_t command) {
    if (!wait_input_empty()) return false;
    outb(STATUS_PORT, command);
    return true;
}

bool mouse_command(uint8_t command) {
    if (!wait_input_empty()) return false;
    outb(STATUS_PORT, 0xD4);
    if (!wait_input_empty()) return false;
    outb(DATA_PORT, command);
    if (!wait_output_full()) return false;
    return inb(DATA_PORT) == 0xFA;
}

void enqueue(Drivers::Mouse::Event event) {
    const uint8_t next = static_cast<uint8_t>((queue_tail + 1) % QUEUE_SIZE);
    if (next == queue_head) return;
    events[queue_tail] = event;
    __asm__ volatile("" ::: "memory");
    queue_tail = next;
}

} // namespace

namespace Drivers {
namespace Mouse {

bool init() {
    available = false;
    error_stage = 1;
    packet_index = 0;
    queue_head = 0;
    queue_tail = 0;

    if (!controller_command(0xA8)) return false;
    error_stage = 2;
    Arch::PIC::disable_irq(1);
    const bool config_ready = controller_command(0x20) && wait_output_full();
    if (!config_ready) {
        Arch::PIC::enable_irq(1);
        return false;
    }
    uint8_t config = inb(DATA_PORT);
    config = static_cast<uint8_t>((config | 0x02) & ~0x20);
    if (!controller_command(0x60) || !wait_input_empty()) {
        Arch::PIC::enable_irq(1);
        return false;
    }
    outb(DATA_PORT, config);
    Arch::PIC::enable_irq(1);

    error_stage = 3;
    if (!mouse_command(0xF6)) return false;
    error_stage = 4;
    if (!mouse_command(0xF4)) return false;
    Arch::PIC::enable_irq(12);
    available = true;
    error_stage = 0;
    return true;
}

bool is_available() {
    return available;
}

uint8_t init_error() {
    return error_stage;
}

void handle_interrupt() {
    const uint8_t status = inb(STATUS_PORT);
    if ((status & 0x20) != 0) {
        const uint8_t value = inb(DATA_PORT);
        if (packet_index == 0 && (value & 0x08) == 0) {
            Arch::PIC::send_eoi(12);
            return;
        }
        packet[packet_index++] = value;
        if (packet_index == 3) {
            packet_index = 0;
            if ((packet[0] & 0xC0) == 0) {
                int delta_x = packet[1];
                int delta_y = packet[2];
                if ((packet[0] & 0x10) != 0) delta_x -= 256;
                if ((packet[0] & 0x20) != 0) delta_y -= 256;
                enqueue({delta_x, -delta_y,
                         static_cast<uint8_t>(packet[0] & 0x07)});
            }
        }
    }
    Arch::PIC::send_eoi(12);
}

std::optional<Event> try_read_event() {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (queue_head == queue_tail) {
        __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
        return std::nullopt;
    }
    Event event = events[queue_head];
    queue_head = static_cast<uint8_t>((queue_head + 1) % QUEUE_SIZE);
    __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
    return event;
}

} // namespace Mouse
} // namespace Drivers

extern "C" void mouse_interrupt_handler() {
    Drivers::Mouse::handle_interrupt();
}