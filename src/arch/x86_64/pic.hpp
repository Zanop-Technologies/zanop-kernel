#ifndef ARCH_X86_64_PIC_HPP
#define ARCH_X86_64_PIC_HPP

#include <cstdint>

namespace Arch {
namespace PIC {

void init();
void send_eoi(uint8_t irq);

} // namespace PIC
} // namespace Arch

#endif