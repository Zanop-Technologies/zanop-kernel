#ifndef MEMORY_ALLOCATOR_HPP
#define MEMORY_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>

namespace Memory {

constexpr std::uintptr_t HEAP_START = 0x444400000ULL;
constexpr std::size_t HEAP_SIZE = 100 * 1024;

void init();
void* allocate(std::size_t size, std::size_t align = alignof(std::max_align_t));
void deallocate(void* ptr);

} // namespace Memory

// Required for any plain `new`/`delete` used anywhere in the kernel --
// without these, such code compiles fine but fails to LINK with an
// undefined-reference error, since -nostdlib provides no default ones.
void* operator new(std::size_t size);
void* operator new[](std::size_t size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;

#endif