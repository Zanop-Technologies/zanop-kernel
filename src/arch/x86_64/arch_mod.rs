pub mod gdt;
pub mod idt;
pub mod pic;

pub fn init() {
    gdt::init();
    pic::init();
    idt::init();

    // Interrupts are now safe to enable — PIC is remapped and masked to
    // only IRQ1, and the IDT has a real handler registered for it.
    unsafe {
        core::arch::asm!("sti");
    }
}