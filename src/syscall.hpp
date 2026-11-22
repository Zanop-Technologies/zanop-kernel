#ifndef KERNEL_SYSCALL_HPP
#define KERNEL_SYSCALL_HPP
#include "types.hpp"
namespace Syscall {
enum Number : uint64_t { Write = 1, Read = 2, Open = 3, Close = 4, Yield = 5, GetPid = 6, Kill = 7, Exit = 8, MapMemory = 9, UnmapMemory = 10 };
enum OpenFlags : uint64_t { ReadOnly = 0, WriteOnly = 1, Create = 2, Truncate = 4, Append = 8 };
long long invoke(uint64_t number, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0);
}
#endif
