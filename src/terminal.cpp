#include "terminal.hpp"
#include "drivers/keyboard.hpp"
#include "drivers/vga.hpp"
#include "drivers/serial.hpp"
#include "signals.hpp"
namespace {
constexpr size_t INPUT_SIZE = 256;
char input[INPUT_SIZE];
volatile size_t head = 0;
volatile size_t tail = 0;
volatile bool interrupted = false;
bool pop(char& out) {
    if (head == tail) return false;
    out = input[head];
    head = (head + 1) % INPUT_SIZE;
    return true;
}
}
namespace Terminal {
void init() { head = tail = 0; interrupted = false; }
void put(char character) {
    if (character == '\n') { VGA::writer.write_byte('\n'); serial::write('\r'); serial::write('\n'); return; }
    if (character == '\b') {
        VGA::writer.backspace();
        serial::write('\b'); serial::write(' '); serial::write('\b');
        return;
    }
    char text[2] = {character, 0}; VGA::print(text); serial::write(character);
}
void write(const char* text) {
    if (!text) return;
    while (*text) { if (*text == '\n') put('\n'); else put(*text); ++text; }
}
void write_bytes(const uint8_t* bytes, size_t length) {
    if (!bytes) return;
    for (size_t i=0;i<length;++i) put(static_cast<char>(bytes[i]));
}
bool try_read(char& character) {
    while (auto scan = Drivers::Keyboard::try_read_scancode()) {
        if (*scan == 0x01) {
            while (head != tail) head = (head + 1) % INPUT_SIZE;
            interrupted = true;
            Signals::raise(0, Signals::INTERRUPT);
            continue;
        }
        auto decoded = Drivers::Keyboard::scancode_to_ascii(*scan);
        if (!decoded) continue;
        const char value = *decoded;
        const size_t next = (tail + 1) % INPUT_SIZE;
        if (next == head) continue;
        input[tail] = value;
        tail = next;
    }
    char serial_character;
    if (serial::try_read(serial_character)) {
        const size_t next = (tail + 1) % INPUT_SIZE;
        if (next != head) { input[tail] = serial_character; tail = next; }
    }
    return pop(character);
}
size_t read(char* buffer, size_t capacity) {
    if (!buffer || capacity == 0) return 0;
    size_t count = 0;
    while (count < capacity) {
        char character;
        if (try_read(character)) buffer[count++] = character;
        else __asm__ volatile("hlt");
    }
    return count;
}
size_t read_line(char* buffer, size_t capacity) {
    if (!buffer || capacity == 0) return 0;
    size_t length = 0;
    interrupted = false;
    while (length + 1 < capacity) {
        char character;
        if (!try_read(character)) { __asm__ volatile("hlt"); continue; }
        if (interrupted || character == 3) {
            interrupted = false;
            head = tail;
            Signals::dispatch();
            return 0;
        }
        if (character == '\b' || character == 127) {
            if (length) { --length; put('\b'); }
            continue;
        }
        if (character == '\n' || character == '\r') {
            buffer[length] = '\0'; put('\n'); return length;
        }
        if (character >= 32 && character <= 126) { buffer[length++] = character; put(character); }
    }
    buffer[length] = '\0';
    return length;
}
void request_interrupt() { interrupted = true; Signals::raise(0, Signals::INTERRUPT); }
}
