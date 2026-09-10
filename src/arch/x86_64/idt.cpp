#include "idt.hpp"

namespace Arch {
namespace IDT {

struct __attribute__((packed)) IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
    
    constexpr IdtEntry() 
        : offset_low(0), selector(0), ist(0), type_attr(0),
          offset_mid(0), offset_high(0), zero(0) {}
    
    void set_handler(uint64_t handler, uint16_t sel = 0x08) {
        offset_low = static_cast<uint16_t>(handler & 0xFFFF);
        offset_mid = static_cast<uint16_t>((handler >> 16) & 0xFFFF);
        offset_high = static_cast<uint32_t>((handler >> 32) & 0xFFFFFFFF);
        selector = sel;
        ist = 0;
        type_attr = 0x8E;  // Present, ring 0, 64-bit interrupt gate
        zero = 0;
    }
};

struct __attribute__((packed)) IdtPointer {
    uint16_t limit;
    uint64_t base;
};

constexpr size_t IDT_ENTRIES = 256;
alignas(16) static IdtEntry idt[IDT_ENTRIES];

void init() {
    for (size_t i = 0; i < IDT_ENTRIES; i++) {
        idt[i] = IdtEntry();
    }
    
    idt[33].set_handler(reinterpret_cast<uint64_t>(isr33));
    
    IdtPointer ptr {
        .limit = sizeof(idt) - 1,
        .base = reinterpret_cast<uint64_t>(&idt)
    };
    
    __builtin_ia32_lidt(&ptr);
}

} // namespace IDT
} // namespace Arch