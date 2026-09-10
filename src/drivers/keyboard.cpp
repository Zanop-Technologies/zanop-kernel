#include "keyboard.hpp"
#include "../arch/x86_64/pic.hpp"
#include <array>

namespace {

constexpr size_t QUEUE_SIZE = 64;
std::array<uint8_t, QUEUE_SIZE> scancode_queue;
size_t queue_head = 0;
size_t queue_tail = 0;

void enqueue_scancode(uint8_t code) {
    size_t next = (queue_tail + 1) % QUEUE_SIZE;
    if (next != queue_head) {
        scancode_queue[queue_tail] = code;
        queue_tail = next;
    }
}

bool dequeue_scancode(uint8_t& out) {
    if (queue_head == queue_tail) {
        return false;
    }
    out = scancode_queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_SIZE;
    return true;
}

inline uint8_t inb(uint16_t port) {
    return __builtin_ia32_inb(port);
}

constexpr uint16_t DATA_PORT = 0x60;

constexpr uint8_t ASCII_MAP[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u',
    'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd', 'f', 'g',
    'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c',
    'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
};

} // anonymous namespace

namespace Drivers {
namespace Keyboard {

void init() {
    // PIC already initialized in Arch::init()
}

void handle_interrupt() {
    uint8_t scancode = inb(DATA_PORT);
    enqueue_scancode(scancode);
    Arch::PIC::send_eoi(1);
}

uint8_t read_scancode() {
    uint8_t code;
    while (!dequeue_scancode(code)) {
        __builtin_ia32_hlt();
    }
    return code;
}

std::optional<uint8_t> try_read_scancode() {
    uint8_t code;
    if (dequeue_scancode(code)) {
        return code;
    }
    return std::nullopt;
}

std::optional<char> read_char() {
    uint8_t code = read_scancode();
    
    if (code & 0x80) {
        return std::nullopt;
    }
    
    if (code < sizeof(ASCII_MAP)) {
        char ch = static_cast<char>(ASCII_MAP[code]);
        if (ch != 0) {
            return ch;
        }
    }
    
    return std::nullopt;
}

} // namespace Keyboard
} // namespace Drivers