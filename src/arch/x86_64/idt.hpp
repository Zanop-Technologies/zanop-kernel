#ifndef ARCH_X86_64_IDT_HPP
#define ARCH_X86_64_IDT_HPP

#include "../../types.hpp"

namespace Arch {
namespace IDT {

void init();

extern "C" void isr33();
extern "C" void isr44();

} // namespace IDT
} // namespace Arch

#endif