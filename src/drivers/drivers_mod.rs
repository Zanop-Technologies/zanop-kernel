pub mod serial;
pub mod vga;
pub mod keyboard;
pub mod rtc;

pub fn init() {
    serial::init();
    vga::init();
    keyboard::init();
}
