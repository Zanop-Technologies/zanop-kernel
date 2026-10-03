#include "apps.hpp"
#include "clock.hpp"
#include "calculator.hpp"
#include "snake.hpp"
#include "notes.hpp"
#include "../drivers/vga.hpp"
#include "../types.hpp"

namespace Apps {

void launch(const char* name) {
    VGA::writer.clear_screen();
    VGA::print("Launching ");
    VGA::println(name);
    VGA::println("(Press ESC to exit)");

    if (strcmp(name, "clock") == 0) Clock::run();
    else if (strcmp(name, "calc") == 0) Calculator::run();
    else if (strcmp(name, "snake") == 0) Snake::run();
    else if (strcmp(name, "notes") == 0) Notes::run();
    else {
        VGA::println("App not found.");
    }

    VGA::writer.clear_screen();
}

} // namespace Apps