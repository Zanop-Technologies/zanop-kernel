//! 64-bit IDT (Interrupt Descriptor Table).
//! Registers isr33 (keyboard, IRQ1 remapped) as a real handler — the
//! previous build only allocated an empty table.

extern "C" {
    fn isr33();
}

#[derive(Clone, Copy)]
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

impl IdtEntry {
    const fn missing() -> Self {
        IdtEntry {
            offset_low: 0,
            selector: 0,
            ist: 0,
            type_attr: 0,
            offset_mid: 0,
            offset_high: 0,
            zero: 0,
        }
    }

    /// `handler` is the address of an extern "C" assembly ISR stub.
    /// `selector` is the code segment selector (from the GDT).
    fn set_handler(&mut self, handler: u64, selector: u16) {
        self.offset_low = handler as u16;
        self.offset_mid = (handler >> 16) as u16;
        self.offset_high = (handler >> 32) as u32;
        self.selector = selector;
        self.ist = 0;
        self.type_attr = 0x8E; // present, ring 0, 64-bit interrupt gate
        self.zero = 0;
    }
}

#[repr(C, packed)]
struct IdtPointer {
    limit: u16,
    base: u64,
}

const IDT_ENTRIES: usize = 256;
static mut IDT: [IdtEntry; IDT_ENTRIES] = [IdtEntry::missing(); IDT_ENTRIES];

pub fn init() {
    unsafe {
        // Code segment selector — matches gdt64.code_segment in boot.asm.
        // That descriptor is the second entry in the GDT, so its selector
        // is 0x08 (index 1 * 8 bytes).
        let code_selector: u16 = 0x08;

        IDT[33].set_handler(isr33 as u64, code_selector);

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