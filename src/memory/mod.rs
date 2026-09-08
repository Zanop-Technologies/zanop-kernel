pub mod allocator;
pub mod paging;

pub fn init() {
    paging::init();
    allocator::init();
}