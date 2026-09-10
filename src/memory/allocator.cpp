#include "allocator.hpp"

namespace Memory {
namespace {
std::uintptr_t next = 0;
std::uintptr_t heap_end = 0;
std::size_t allocations = 0;

std::uintptr_t align_up(std::uintptr_t addr, std::size_t align) {
    return (addr + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
}
}

void init() {
    next = HEAP_START;
    heap_end = HEAP_START + HEAP_SIZE;
}

void* allocate(std::size_t size, std::size_t align) {
    std::uintptr_t start = align_up(next, align);
    std::uintptr_t end = start + size;
    if (end > heap_end) return nullptr;
    next = end;
    ++allocations;
    return reinterpret_cast<void*>(start);
}

void deallocate(void* ptr) {
    if (!ptr) return;
    if (allocations > 0) --allocations;
    if (allocations == 0) next = HEAP_START; // bump allocator's only reclaim path
}

} // namespace Memory

void* operator new(std::size_t size) { return Memory::allocate(size); }
void* operator new[](std::size_t size) { return Memory::allocate(size); }
void operator delete(void* ptr) noexcept { Memory::deallocate(ptr); }
void operator delete[](void* ptr) noexcept { Memory::deallocate(ptr); }