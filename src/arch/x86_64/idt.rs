#[repr(C, packed)]
struct IdtEntry {
    offset_low: u16,
    selector: u16,
    ist: u8,
    type_attr: u8,
    offset_mid: u16,
    offset_high: u32,
    zero: u32,
}

#[repr(C, packed)]
struct IdtPointer {
    limit: u16,
    base: u64,
}

const IDT_ENTRIES: usize = 256;
static mut IDT: [IdtEntry; IDT_ENTRIES] = [IdtEntry {
    offset_low: 0,
    selector: 0,
    ist: 0,
    type_attr: 0,
    offset_mid: 0,
    offset_high: 0,
    zero: 0,
}; IDT_ENTRIES];

pub fn init() {
    unsafe {
        let ptr = IdtPointer {
            limit: (core::mem::size_of_val(&IDT) - 1) as u16,
            base: IDT.as_ptr() as u64,
        };
        load_idt(&ptr);
    }
}

unsafe fn load_idt(ptr: &IdtPointer) {
    core::arch::asm!("lidt [{}]", in(reg) ptr, options(readonly, nostack, preserves_flags));
}