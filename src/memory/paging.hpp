#ifndef MEMORY_PAGING_HPP
#define MEMORY_PAGING_HPP

#include "../types.hpp"

namespace Memory {
namespace Paging {

void init();
uint64_t current_page_table_addr();
bool map_page(uintptr_t virtual_address, uintptr_t physical_address, uint64_t flags);
bool unmap_page(uintptr_t virtual_address);
uintptr_t translate(uintptr_t virtual_address);

} // namespace Paging
} // namespace Memory

#endif