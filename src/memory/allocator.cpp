#include "allocator.hpp"
#include "paging.hpp"

namespace Memory {
namespace {
struct Block { std::size_t size; Block* next; bool free; };
constexpr std::uintptr_t HEAP_END = HEAP_START + HEAP_SIZE;
Block* first = nullptr;
bool initialized = false;

std::uintptr_t align_up(std::uintptr_t value, std::size_t alignment) {
    if (alignment < alignof(void*)) alignment = alignof(void*);
    if ((alignment & (alignment - 1)) != 0) return 0;
    if (value > static_cast<std::uintptr_t>(-1) - (alignment - 1)) return 0;
    return (value + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
}
void split(Block* block, std::size_t requested) {
    if (requested > static_cast<std::size_t>(-1) - sizeof(Block) - alignof(void*) ||
        block->size < requested + sizeof(Block) + alignof(void*)) return;
    Block* tail = reinterpret_cast<Block*>(reinterpret_cast<std::uintptr_t>(block + 1) + requested);
    tail->size = block->size - requested - sizeof(Block);
    tail->next = block->next;
    tail->free = true;
    block->size = requested;
    block->next = tail;
}
void coalesce() {
    for (Block* block = first; block && block->next;) {
        if (block->free && block->next->free) {
            block->size += sizeof(Block) + block->next->size;
            block->next = block->next->next;
        } else block = block->next;
    }
}
}

void init() {
    Paging::init();
    first = reinterpret_cast<Block*>(HEAP_START);
    first->size = HEAP_SIZE - sizeof(Block);
    first->next = nullptr;
    first->free = true;
    initialized = true;
}

void* allocate(std::size_t size, std::size_t align) {
    if (!initialized) init();
    if (size == 0) size = 1;
    if (align < alignof(void*)) align = alignof(void*);
    if ((align & (align - 1)) != 0 || size > HEAP_SIZE) return nullptr;
    for (Block* block = first; block; block = block->next) {
        if (!block->free) continue;
        const std::uintptr_t payload = align_up(reinterpret_cast<std::uintptr_t>(block + 1), align);
        if (!payload) return nullptr;
        const std::size_t prefix = payload - reinterpret_cast<std::uintptr_t>(block + 1);
        if (block->size < prefix || block->size - prefix < size) continue;
        if (prefix >= sizeof(Block) + alignof(void*)) {
            Block* aligned = reinterpret_cast<Block*>(payload - sizeof(Block));
            aligned->size = block->size - prefix;
            aligned->next = block->next;
            aligned->free = true;
            block->size = prefix - sizeof(Block);
            block->next = aligned;
            block->free = true;
            split(aligned, size);
            aligned->free = false;
            return reinterpret_cast<void*>(payload);
        }
        if (prefix) continue;
        split(block, size);
        block->free = false;
        return reinterpret_cast<void*>(payload);
    }
    return nullptr;
}

void deallocate(void* ptr) {
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(ptr);
    if (!initialized || !ptr || address < HEAP_START + sizeof(Block) || address >= HEAP_END) return;
    for (Block* block = first; block; block = block->next) {
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(block + 1);
        if (address >= start && address < start + block->size && !block->free) {
            block->free = true;
            coalesce();
            return;
        }
    }
}

std::size_t free_bytes() {
    if (!initialized) return HEAP_SIZE - sizeof(Block);
    std::size_t total = 0;
    for (Block* block = first; block; block = block->next) if (block->free) total += block->size;
    return total;
}

} // namespace Memory

void* operator new(std::size_t size) { return Memory::allocate(size); }
void* operator new[](std::size_t size) { return Memory::allocate(size); }
void operator delete(void* ptr) noexcept { Memory::deallocate(ptr); }
void operator delete[](void* ptr) noexcept { Memory::deallocate(ptr); }