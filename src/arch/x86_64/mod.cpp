#include "pic.hpp"
#include "idt.hpp"

namespace Arch {

void init() {
    PIC::init();
    IDT::init();
}

} // namespace Arch