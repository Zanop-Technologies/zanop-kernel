use core::fmt;

const COM1: u16 = 0x3F8;

pub struct SerialPort;

impl SerialPort {
    unsafe fn write_byte(&self, byte: u8) {
        while (inb(COM1 + 5) & 0x20) == 0 {} // wait for transmit buffer empty
        outb(COM1, byte);
    }
}

impl fmt::Write for SerialPort {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for byte in s.bytes() {
            unsafe { self.write_byte(byte) };
        }
        Ok(())
    }
}

unsafe fn outb(port: u16, val: u8) {
    core::arch::asm!("out dx, al", in("dx") port, in("al") val);
}

unsafe fn inb(port: u16) -> u8 {
    let val: u8;
    core::arch::asm!("in al, dx", in("dx") port, out("al") val);
    val
}

pub fn init() {
    unsafe {
        outb(COM1 + 1, 0x00); // disable interrupts
        outb(COM1 + 3, 0x80); // enable DLAB
        outb(COM1 + 0, 0x03); // divisor low byte (38400 baud)
        outb(COM1 + 1, 0x00); // divisor high byte
        outb(COM1 + 3, 0x03); // 8 bits, no parity, one stop bit
        outb(COM1 + 2, 0xC7); // enable FIFO
        outb(COM1 + 4, 0x0B); // IRQs enabled, RTS/DSR set
    }
}