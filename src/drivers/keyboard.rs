const DATA_PORT: u16 = 0x60;

pub fn init() {
    // PS/2 controller init would go here (0x64 command port).
    // Left minimal for now — interrupt-driven input hooks in via
    // arch::x86_64::idt once ISR33 (IRQ1) is wired up.
}

pub fn read_scancode() -> u8 {
    unsafe { inb(DATA_PORT) }
}

/// Very small US QWERTY scancode->ASCII table (set 1, key-down only).
pub fn scancode_to_ascii(code: u8) -> Option<u8> {
    const MAP: [u8; 58] = [
        0, 0, b'1', b'2', b'3', b'4', b'5', b'6', b'7', b'8', b'9', b'0',
        b'-', b'=', 0x08, b'\t', b'q', b'w', b'e', b'r', b't', b'y', b'u',
        b'i', b'o', b'p', b'[', b']', b'\n', 0, b'a', b's', b'd', b'f', b'g',
        b'h', b'j', b'k', b'l', b';', b'\'', b'`', 0, b'\\', b'z', b'x', b'c',
        b'v', b'b', b'n', b'm', b',', b'.', b'/', 0, b'*', 0, b' ',
    ];
    MAP.get(code as usize).copied().filter(|&b| b != 0)
}

unsafe fn inb(port: u16) -> u8 {
    let val: u8;
    core::arch::asm!("in al, dx", in("dx") port, out("al") val);
    val
}