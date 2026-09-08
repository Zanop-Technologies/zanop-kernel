pub mod serial;
pub mod vga;
pub mod keyboard;

pub fn init() {
    serial::init();
    vga::init();
    keyboard::init();
}