//! PS/2 keyboard driver — now interrupt-driven (IRQ1) instead of raw
//! polling. The previous build read port 0x60 directly with no edge
//! detection, causing repeats/garbage. Now the port is only ever read
//! inside the actual interrupt handler, which only fires once per real
//! hardware event.

use alloc::collections::VecDeque;
use spin::Mutex;
use lazy_static::lazy_static;
use crate::arch::x86_64::pic;

const DATA_PORT: u16 = 0x60;

unsafe fn inb(port: u16) -> u8 {
    let val: u8;
    core::arch::asm!("in al, dx", in("dx") port, out("al") val);
    val
}

lazy_static! {
    static ref SCANCODE_QUEUE: Mutex<VecDeque<u8>> = Mutex::new(VecDeque::with_capacity(64));
}

pub fn init() {
    // PIC remap/masking happens in arch::x86_64::pic::init(), called
    // before this from arch::x86_64::init(). Nothing else to configure
    // for a basic PS/2 keyboard.
}

/// Called directly from the isr33 assembly stub on every real IRQ1 event.
/// Reads the scancode exactly once, queues it, and acknowledges the PIC.
#[no_mangle]
pub extern "C" fn keyboard_interrupt_handler() {
    let scancode = unsafe { inb(DATA_PORT) };

    let mut queue = SCANCODE_QUEUE.lock();
    if queue.len() < 64 {
        queue.push_back(scancode);
    }
    drop(queue);

    pic::send_eoi(1);
}

/// Blocking read — waits for a real interrupt-delivered scancode.
/// Uses `hlt` between checks instead of busy-spinning, so the CPU
/// actually sleeps until the next interrupt (any interrupt) fires.
pub fn read_scancode() -> u8 {
    loop {
        if let Some(code) = SCANCODE_QUEUE.lock().pop_front() {
            return code;
        }
        unsafe { core::arch::asm!("hlt") };
    }
}

/// Non-blocking read — returns immediately whether or not a key was
/// pressed. Needed by anything that must keep running between keypresses
/// (Snake's movement loop, Clock's ticking display).
pub fn try_read_scancode() -> Option<u8> {
    SCANCODE_QUEUE.lock().pop_front()
}

/// Very small US QWERTY scancode->ASCII table (set 1, key-down only).
/// Release codes (press code + 0x80) are always >= 0x80 = 128, which is
/// past this table's length, so `.get()` naturally filters them out —
/// no separate release-code check needed.
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