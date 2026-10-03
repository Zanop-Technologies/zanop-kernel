#include "panic.hpp"
#include "drivers/serial.hpp"
#include "drivers/vga.hpp"

void panic_handler(const PanicInfo& info) {
    // Disable interrupts
    __asm__ volatile("cli");
    
    // Write to serial
    serial::write_string("KERNEL PANIC: ");
    serial::write_string(info.message);
    serial::write_string("\n");
    
    // Write to VGA
    VGA::println("KERNEL PANIC!");
    VGA::print("Error: ");
    VGA::println(info.message);
    
    // Halt
    while (true) {
        __asm__ volatile("hlt");
    }
}