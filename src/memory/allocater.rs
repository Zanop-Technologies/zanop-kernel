use core::alloc::{GlobalAlloc, Layout};
use core::cell::UnsafeCell;
use spin::Mutex;

pub const HEAP_START: usize = 0x_4444_4444_0000;
pub const HEAP_SIZE: usize = 100 * 1024; // 100 KiB to start

pub struct BumpAllocator {
    heap_start: usize,
    heap_end: usize,
    next: UnsafeCell<usize>,
    allocations: UnsafeCell<usize>,
}

unsafe impl Sync for BumpAllocator {}

impl BumpAllocator {
    pub const fn new() -> Self {
        BumpAllocator {
            heap_start: 0,
            heap_end: 0,
            next: UnsafeCell::new(0),
            allocations: UnsafeCell::new(0),
        }
    }

    pub unsafe fn init(&mut self, heap_start: usize, heap_size: usize) {
        self.heap_start = heap_start;
        self.heap_end = heap_start + heap_size;
        *self.next.get() = heap_start;
    }
}

unsafe impl GlobalAlloc for Mutex<BumpAllocator> {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let mut bump = self.lock();
        let alloc_start = align_up(*bump.next.get(), layout.align());
        let alloc_end = match alloc_start.checked_add(layout.size()) {
            Some(end) => end,
            None => return core::ptr::null_mut(),
        };

        if alloc_end > bump.heap_end {
            core::ptr::null_mut()
        } else {
            *bump.next.get() = alloc_end;
            *bump.allocations.get() += 1;
            alloc_start as *mut u8
        }
    }

    unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {
        let bump = self.lock();
        *bump.allocations.get() -= 1;
        if *bump.allocations.get() == 0 {
            *bump.next.get() = bump.heap_start;
        }
    }
}

#[global_allocator]
static ALLOCATOR: Mutex<BumpAllocator> = Mutex::new(BumpAllocator::new());

pub fn init() {
    unsafe {
        ALLOCATOR.lock().init(HEAP_START, HEAP_SIZE);
    }
}

fn align_up(addr: usize, align: usize) -> usize {
    (addr + align - 1) & !(align - 1)
}