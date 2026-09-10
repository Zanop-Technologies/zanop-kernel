#include "paging.hpp"

namespace Memory {
namespace Paging {

void init() {
    // TODO: Read CR3, walk/construct PML4 -> PDPT -> PD -> PT
    // TODO: Identity-map or higher-half map kernel sections
}

uint64_t current_page_table_addr() {
    uint64_t addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(addr));
    return addr;
}

} // namespace Paging
} // namespace Memory