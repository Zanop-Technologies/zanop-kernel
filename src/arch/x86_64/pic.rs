//! 8259 PIC (Programmable Interrupt Controller) driver.
//! By default IRQ0-7 map to interrupt vectors 0-7, which collides with
//! CPU exceptions (divide error, etc). This remaps them to 32-47.

const PIC1_COMMAND: u16 = 0x20;
const PIC1_DATA: u16 = 0x21;
const PIC2_COMMAND: u16 = 0xA0;
const PIC2_DATA: u16 = 0xA1;

const ICW1_INIT: u8 = 0x10;
const ICW1_ICW4: u8 = 0x01;
const ICW4_8086: u8 = 0x01;

const PIC1_OFFSET: u8 = 32; // IRQ0-7  -> vectors 32-39
const PIC2_OFFSET: u8 = 40; // IRQ8-15 -> vectors 40-47

unsafe fn outb(port: u16, val: u8) {
    core::arch::asm!("out dx, al", in("dx") port, in("al") val);
}

unsafe fn inb(port: u16) -> u8 {
    let val: u8;
    core::arch::asm!("in al, dx", in("dx") port, out("al") val);
    val
}

unsafe fn io_wait() {
    outb(0x80, 0);
}

/// Remaps both PICs, then masks everything except IRQ1 (keyboard) —
/// other IRQs (timer, mouse, etc.) get unmasked as their drivers land.
pub fn init() {
    unsafe {
        let mask1 = inb(PIC1_DATA);
        let mask2 = inb(PIC2_DATA);

        outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
        io_wait();
        outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
        io_wait();

        outb(PIC1_DATA, PIC1_OFFSET);
        io_wait();
        outb(PIC2_DATA, PIC2_OFFSET);
        io_wait();

        outb(PIC1_DATA, 4); // tell master PIC there's a slave at IRQ2
        io_wait();
        outb(PIC2_DATA, 2); // tell slave PIC its cascade identity
        io_wait();

        outb(PIC1_DATA, ICW4_8086);
        io_wait();
        outb(PIC2_DATA, ICW4_8086);
        io_wait();

        let _ = (mask1, mask2); // original masks discarded — see below

        // Mask everything except IRQ1 (keyboard). 0xFD = 11111101 (IRQ1 clear).
        outb(PIC1_DATA, 0xFD);
        outb(PIC2_DATA, 0xFF);
    }
}

/// Send End-Of-Interrupt. Must be called at the end of every IRQ handler
/// or the PIC will never deliver another interrupt on that line.
pub fn send_eoi(irq: u8) {
    unsafe {
        if irq >= 8 {
            outb(PIC2_COMMAND, 0x20);
        }
        outb(PIC1_COMMAND, 0x20);
    }
}