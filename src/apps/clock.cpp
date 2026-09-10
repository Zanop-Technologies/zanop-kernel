#include "clock.hpp"
#include "../drivers/vga.hpp"
#include "../drivers/keyboard.hpp"

namespace {
    inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
    inline uint8_t inb(uint16_t p) { uint8_t r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(p)); return r; }

    uint8_t get_rtc(uint8_t reg) { outb(0x70, reg); return inb(0x71); }
    
    uint8_t bcd_to_bin(uint8_t val) { return (val & 0xF) + ((val >> 4) * 10); }
}

namespace Clock {
void run() {
    while (true) {
        if (Drivers::Keyboard::try_read_scancode().value_or(0) == 0x01) return; // ESC

        uint8_t sec = bcd_to_bin(get_rtc(0x00));
        uint8_t min = bcd_to_bin(get_rtc(0x02));
        uint8_t hr  = bcd_to_bin(get_rtc(0x04));

        char buf[32];
        // Simple itoa replacement for 2 digits
        buf[0] = '0' + (hr / 10); buf[1] = '0' + (hr % 10); buf[2] = ':';
        buf[3] = '0' + (min / 10); buf[4] = '0' + (min % 10); buf[5] = ':';
        buf[6] = '0' + (sec / 10); buf[7] = '0' + (sec % 10); buf[8] = '\0';

        VGA::print("\rTime: ");
        VGA::print(buf);
        
        // Simple delay
        for (volatile int i = 0; i < 1000000; i++); 
    }
}
}