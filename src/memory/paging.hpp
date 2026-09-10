#ifndef MEMORY_PAGING_HPP
#define MEMORY_PAGING_HPP

#include <cstdint>

namespace Memory {
namespace Paging {

void init();
uint64_t current_page_table_addr();

} // namespace Paging
} // namespace Memory

#endif