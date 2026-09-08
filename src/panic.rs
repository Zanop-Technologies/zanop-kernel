use core::panic::PanicInfo;
use core::fmt::Write;

pub fn panic_handler(info: &PanicInfo) -> ! {
    // Write panic info to serial (always available, even if VGA is broken)
    let mut serial = crate::drivers::serial::SerialPort;
    let _ = writeln!(serial, "KERNEL PANIC: {}", info);

    loop {
        unsafe { core::arch::asm!("hlt") };
    }
}