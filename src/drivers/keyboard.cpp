#include "keyboard.hpp"
#include "../arch/x86_64/pic.hpp"

namespace {

constexpr size_t QUEUE_SIZE = 64;
uint8_t scancode_queue[QUEUE_SIZE];
size_t queue_head = 0;
size_t queue_tail = 0;
bool shift_down = false;
bool caps_lock = false;

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
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
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
        __asm__ volatile("hlt");
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

std::optional<char> scancode_to_ascii(uint8_t code) {
    if (code == 0x2A || code == 0x36) {
        shift_down = true;
        return std::nullopt;
    }
    if (code == 0xAA || code == 0xB6) {
        shift_down = false;
        return std::nullopt;
    }
    if (code == 0x3A) {
        caps_lock = !caps_lock;
        return std::nullopt;
    }
    if (code & 0x80) {
        return std::nullopt;
    }
    
    if (code < sizeof(ASCII_MAP)) {
        char ch = static_cast<char>(ASCII_MAP[code]);
        if (ch != 0) {
            if (ch >= 'a' && ch <= 'z') {
                if (shift_down != caps_lock) ch -= 'a' - 'A';
            } else if (shift_down) {
                switch (ch) {
                    case '1': ch = '!'; break;
                    case '2': ch = '@'; break;
                    case '3': ch = '#'; break;
                    case '4': ch = '$'; break;
                    case '5': ch = '%'; break;
                    case '6': ch = '^'; break;
                    case '7': ch = '&'; break;
                    case '8': ch = '*'; break;
                    case '9': ch = '('; break;
                    case '0': ch = ')'; break;
                    case '-': ch = '_'; break;
                    case '=': ch = '+'; break;
                    case '[': ch = '{'; break;
                    case ']': ch = '}'; break;
                    case ';': ch = ':'; break;
                    case '\'': ch = '"'; break;
                    case '`': ch = '~'; break;
                    case '\\': ch = '|'; break;
                    case ',': ch = '<'; break;
                    case '.': ch = '>'; break;
                    case '/': ch = '?'; break;
                    default: break;
                }
            }
            return ch;
        }
    }
    
    return std::nullopt;
}

std::optional<char> read_char() {
    return scancode_to_ascii(read_scancode());
}

} // namespace Keyboard
} // namespace Drivers

extern "C" void keyboard_interrupt_handler() {
    Drivers::Keyboard::handle_interrupt();
}