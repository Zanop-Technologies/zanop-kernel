#![no_std]
#![no_main]

extern crate alloc;

mod arch;
mod drivers;
mod memory;
mod panic;
mod task;

use core::panic::PanicInfo;

#[no_mangle]
pub extern "C" fn kernel_main() -> ! {
    drivers::serial::init();
    drivers::vga::init();

    memory::init();
    arch::x86_64::init();
    drivers::keyboard::init();
    task::init();

    // TODO: enable interrupts once IDT handlers are fully wired
    // TODO: main kernel loop / shell

    loop {
        unsafe { core::arch::asm!("hlt") };
    }
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    panic::panic_handler(info)
}