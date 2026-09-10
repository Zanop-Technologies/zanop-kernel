#include "panic.hpp"
#include "drivers/serial.hpp"
#include "drivers/vga.hpp"

void panic_handler(const PanicInfo& info) {
    // Disable interrupts
    __asm__ volatile("cli");
    
    // Write to serial
    Drivers::Serial::write_string("KERNEL PANIC: ");
    Drivers::Serial::write_string(info.message);
    Drivers::Serial::write_string("\n");
    
    // Write to VGA
    VGA::println("KERNEL PANIC!");
    VGA::print("Error: ");
    VGA::println(info.message);
    
    // Halt
    while (true) {
        __builtin_ia32_hlt();
    }
}