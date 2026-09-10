#include "drivers/vga.hpp"
#include "drivers/keyboard.hpp"
#include "drivers/serial.hpp"
#include "arch/x86_64/pic.hpp"
#include "arch/x86_64/idt.hpp"
#include "memory/allocator.hpp"
#include "panic.hpp"

extern "C" {

void kernel_main() {
    Drivers::Serial::init();
    VGA::init();
    
    VGA::println("Zanop Kernel v0.0.5 (C++ Edition)");
    VGA::println("Initializing subsystems...");
    
    Memory::init();
    
    // Initialize architecture (PIC + IDT)
    Arch::init();
    
    Drivers::Keyboard::init();
    
    // TODO: Enable interrupts
    // __asm__ volatile("sti");
    
    VGA::println("Kernel initialized successfully!");
    VGA::println("Press any key...");
    
    // Simple test
    while (true) {
        auto key = Drivers::Keyboard::try_read_scancode();
        if (key.has_value()) {
            VGA::print("Key pressed! Scancode: ");
            // Convert to hex and print
            __builtin_ia32_hlt();
        }
        __builtin_ia32_hlt();
    }
}

void panic(const PanicInfo& info) {
    panic_handler(info);
}

}