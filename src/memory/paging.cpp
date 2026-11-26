#include "paging.hpp"

namespace Memory {
namespace Paging {

namespace {
constexpr uint64_t PRESENT = 1;
constexpr uint64_t WRITABLE = 2;
constexpr uint64_t USER = 1ull << 2;
constexpr uint64_t HUGE_PAGE = 1ull << 7;
constexpr uintptr_t ADDRESS_MASK = 0x000FFFFFFFFFF000ull;
uint64_t* root = nullptr;
constexpr uintptr_t KERNEL_HEAP_START = 0x444400000ull;
constexpr uintptr_t PAGE_SIZE = 4096;
constexpr uintptr_t LARGE_PAGE_SIZE = 0x200000;
}

void init() {
    root = reinterpret_cast<uint64_t*>(current_page_table_addr() & ADDRESS_MASK);
    for (uintptr_t offset=0; offset<100*1024; offset+=PAGE_SIZE) {
        if (translate(KERNEL_HEAP_START+offset) != 0) continue;
        const uintptr_t physical=KERNEL_HEAP_START+offset;
        if (!map_page(physical,physical,WRITABLE)) break;
    }
}

uint64_t current_page_table_addr() {
    uint64_t addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(addr));
    return addr;
}

bool map_page(uintptr_t virtual_address, uintptr_t physical_address, uint64_t flags) {
    if (!root || (virtual_address & 0xFFF) || (physical_address & 0xFFF)) return false;
    if (virtual_address >= KERNEL_HEAP_START && virtual_address < KERNEL_HEAP_START + 100*1024) return false;
    const size_t indexes[4] = {
        static_cast<size_t>((virtual_address >> 39) & 0x1FF),
        static_cast<size_t>((virtual_address >> 30) & 0x1FF),
        static_cast<size_t>((virtual_address >> 21) & 0x1FF),
        static_cast<size_t>((virtual_address >> 12) & 0x1FF)
    };
    uint64_t* table = root;
    for (size_t level = 0; level < 3; ++level) {
        uint64_t entry = table[indexes[level]];
        if (!(entry & PRESENT) || (entry & HUGE_PAGE)) return false;
        entry |= WRITABLE;
        table[indexes[level]] = entry;
        table = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(entry & ADDRESS_MASK));
    }
    uint64_t& leaf = table[indexes[3]];
    if (leaf & PRESENT) return false;
    leaf = (physical_address & ADDRESS_MASK) | (flags & 0xFFF) | PRESENT;
    __asm__ volatile("invlpg (%0)" :: "r"(virtual_address) : "memory");
    return true;
}

bool unmap_page(uintptr_t virtual_address) {
    if (!root || (virtual_address & 0xFFF)) return false;
    const size_t indexes[4] = {
        static_cast<size_t>((virtual_address >> 39) & 0x1FF),
        static_cast<size_t>((virtual_address >> 30) & 0x1FF),
        static_cast<size_t>((virtual_address >> 21) & 0x1FF),
        static_cast<size_t>((virtual_address >> 12) & 0x1FF)
    };
    uint64_t* table = root;
    for (size_t level = 0; level < 3; ++level) {
        const uint64_t entry = table[indexes[level]];
        if (!(entry & PRESENT) || (entry & HUGE_PAGE)) return false;
        table = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(entry & ADDRESS_MASK));
    }
    uint64_t& leaf = table[indexes[3]];
    if (!(leaf & PRESENT)) return false;
    leaf = 0;
    __asm__ volatile("invlpg (%0)" :: "r"(virtual_address) : "memory");
    return true;
}

bool make_user_accessible(uintptr_t virtual_address) {
    if (!root || (virtual_address & (LARGE_PAGE_SIZE - 1))) return false;
    uint64_t& l4 = root[(virtual_address >> 39) & 0x1FF];
    if (!(l4 & PRESENT)) return false;
    uint64_t* l3_table = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(l4 & ADDRESS_MASK));
    uint64_t& l3 = l3_table[(virtual_address >> 30) & 0x1FF];
    if (!(l3 & PRESENT) || (l3 & HUGE_PAGE)) return false;
    uint64_t* l2_table = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(l3 & ADDRESS_MASK));
    uint64_t& l2 = l2_table[(virtual_address >> 21) & 0x1FF];
    if (!(l2 & PRESENT) || !(l2 & HUGE_PAGE)) return false;
    l4 |= USER;
    l3 |= USER;
    l2 |= USER;
    // Reload CR3: flushes every cached translation for the changed entries.
    __asm__ volatile("mov %%cr3, %%rax\n\tmov %%rax, %%cr3" ::: "rax", "memory");
    return true;
}

uintptr_t translate(uintptr_t virtual_address) {
    if (!root) return 0;
    const size_t indexes[4] = {
        static_cast<size_t>((virtual_address >> 39) & 0x1FF),
        static_cast<size_t>((virtual_address >> 30) & 0x1FF),
        static_cast<size_t>((virtual_address >> 21) & 0x1FF),
        static_cast<size_t>((virtual_address >> 12) & 0x1FF)
    };
    uint64_t* table = root;
    for (size_t level = 0; level < 4; ++level) {
        const uint64_t entry = table[indexes[level]];
        if (!(entry & PRESENT)) return 0;
        if (level == 1 && (entry & HUGE_PAGE))
            return static_cast<uintptr_t>((entry & 0x000FFFFFC0000ull) | (virtual_address & 0x3FFFFFFFull));
        if (level == 2 && (entry & HUGE_PAGE))
            return static_cast<uintptr_t>((entry & 0x000FFFFFFFE00000ull) | (virtual_address & 0x1FFFFFull));
        if (level == 3) return static_cast<uintptr_t>((entry & ADDRESS_MASK) | (virtual_address & 0xFFF));
        table = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(entry & ADDRESS_MASK));
    }
    return 0;
}

} // namespace Paging
} // namespace Memory