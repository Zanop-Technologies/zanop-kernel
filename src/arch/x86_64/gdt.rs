#[repr(C, packed)]
struct GdtEntry {
    limit_low: u16,
    base_low: u16,
    base_middle: u8,
    access: u8,
    granularity: u8,
    base_high: u8,
}

#[repr(C, packed)]
struct GdtPointer {
    limit: u16,
    base: u64,
}

static mut GDT: [GdtEntry; 3] = [
    // null descriptor
    GdtEntry { limit_low: 0, base_low: 0, base_middle: 0, access: 0, granularity: 0, base_high: 0 },
    // kernel code segment
    GdtEntry { limit_low: 0xFFFF, base_low: 0, base_middle: 0, access: 0x9A, granularity: 0xAF, base_high: 0 },
    // kernel data segment
    GdtEntry { limit_low: 0xFFFF, base_low: 0, base_middle: 0, access: 0x92, granularity: 0xAF, base_high: 0 },
];

pub fn init() {
    unsafe {
        let ptr = GdtPointer {
            limit: (core::mem::size_of_val(&GDT) - 1) as u16,
            base: GDT.as_ptr() as u64,
        };
        load_gdt(&ptr);
    }
}

unsafe fn load_gdt(ptr: &GdtPointer) {
    core::arch::asm!("lgdt [{}]", in(reg) ptr, options(readonly, nostack, preserves_flags));
}