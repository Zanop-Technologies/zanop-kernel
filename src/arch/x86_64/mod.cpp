#include "pic.hpp"
#include "idt.hpp"

namespace Arch {

void init() {
    PIC::init();
    IDT::init();
    __asm__ volatile("sti" ::: "memory");
}

} // namespace Arch