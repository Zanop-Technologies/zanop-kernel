#ifndef ARCH_X86_64_PIC_HPP
#define ARCH_X86_64_PIC_HPP

#include "../../types.hpp"

namespace Arch {

void init();

namespace PIC {

void init();
void send_eoi(uint8_t irq);
void enable_irq(uint8_t irq);
void disable_irq(uint8_t irq);

} // namespace PIC
} // namespace Arch

#endif