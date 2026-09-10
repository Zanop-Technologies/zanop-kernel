#ifndef MEMORY_ALLOCATOR_HPP
#define MEMORY_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>

namespace Memory {
namespace Allocator {

void init();
void* allocate(size_t size);
void deallocate(void* ptr);

// Global allocator interface
class GlobalAllocator {
public:
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);
};

extern GlobalAllocator global_allocator;

} // namespace Allocator
} // namespace Memory

#endif