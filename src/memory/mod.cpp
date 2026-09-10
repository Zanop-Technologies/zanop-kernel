#include "paging.hpp"
#include "allocator.hpp"

namespace Memory {

void init() {
    Paging::init();
    Allocator::init();
}

} // namespace Memory