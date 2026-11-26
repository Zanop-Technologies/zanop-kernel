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
};

struct __attribute__((packed)) IdtPointer {
    uint16_t limit;
    uint64_t base;
};

constexpr size_t IDT_ENTRIES = 256;
static IdtEntry idt[IDT_ENTRIES] __attribute__((aligned(16)));

static void set_gate(size_t vector, uint64_t handler) {
    idt[vector].offset_low = static_cast<uint16_t>(handler);
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].type_attr = 0x8E;
    idt[vector].offset_mid = static_cast<uint16_t>(handler >> 16);
    idt[vector].offset_high = static_cast<uint32_t>(handler >> 32);
    idt[vector].zero = 0;
}

void init() {
    for (size_t index = 0; index < IDT_ENTRIES; ++index) {
        idt[index] = {};
    }

    // CPU exceptions 0..31 (stubs in usermode.asm). Without these a fault
    // triple-faults and silently resets the machine.
    for (size_t vector = 0; vector < 32; ++vector) {
        set_gate(vector, zanop_exception_stubs[vector]);
    }

    uint64_t handler = reinterpret_cast<uint64_t>(isr33);
    idt[33].offset_low = static_cast<uint16_t>(handler);
    idt[33].selector = 0x08;
    idt[33].type_attr = 0x8E;
    idt[33].offset_mid = static_cast<uint16_t>(handler >> 16);
    idt[33].offset_high = static_cast<uint32_t>(handler >> 32);

    handler = reinterpret_cast<uint64_t>(isr44);
    idt[44].offset_low = static_cast<uint16_t>(handler);
    idt[44].selector = 0x08;
    idt[44].type_attr = 0x8E;
    idt[44].offset_mid = static_cast<uint16_t>(handler >> 16);
    idt[44].offset_high = static_cast<uint32_t>(handler >> 32);

    handler = reinterpret_cast<uint64_t>(isr43);
    idt[43].offset_low = static_cast<uint16_t>(handler);
    idt[43].selector = 0x08;
    idt[43].type_attr = 0x8E;
    idt[43].offset_mid = static_cast<uint16_t>(handler >> 16);
    idt[43].offset_high = static_cast<uint32_t>(handler >> 32);

    IdtPointer pointer{static_cast<uint16_t>(sizeof(idt) - 1),
                       reinterpret_cast<uint64_t>(&idt)};
    __asm__ volatile("lidt %0" : : "m"(pointer));
}

} // namespace IDT
} // namespace Arch