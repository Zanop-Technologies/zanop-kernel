#ifndef ARCH_X86_64_IDT_HPP
#define ARCH_X86_64_IDT_HPP

#include <cstdint>

namespace Arch {
namespace IDT {

void init();

extern "C" {
    void isr33();  // Keyboard handler
}

} // namespace IDT
} // namespace Arch

#endif