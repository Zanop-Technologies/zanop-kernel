#include "notes.hpp"
#include "../drivers/vga.hpp"
#include "../drivers/keyboard.hpp"
#include "../fs/fs.hpp"
#include <cstring>

namespace Notes {

void run() {
    char buf[256];
    size_t pos = 0;
    memset(buf, 0, 256);

    VGA::println("Type your note. Press ESC to save & exit.");
    VGA::print("> ");

    while (true) {
        auto key = Drivers::Keyboard::read_char();
        if (!key.has_value()) continue;
        char c = key.value();

        if (c == 0x01) {
            buf[pos] = '\0';
            FS::create_file("note.txt", buf);
            VGA::println("\nNote saved to note.txt");
            return;
        }
        else if (c == '\n') {
            if (pos < 255) buf[pos++] = '\n';
            VGA::print("\n> ");
        }
        else if (c == '\b') {
            if (pos > 0) { pos--; buf[pos] = '\0'; VGA::print("\b \b"); }
        }
        else if (pos < 255) {
            buf[pos++] = c;
            char str[2] = {c, '\0'};
            VGA::print(str);
        }
    }
}

} // namespace Notes