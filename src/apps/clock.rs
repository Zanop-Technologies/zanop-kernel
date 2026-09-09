use crate::{print, println};
use crate::drivers::rtc;
use crate::drivers::keyboard;

pub fn run() {
    println!();
    println!("--- Clock ---");
    println!("Showing real time from the CMOS RTC. Press any key to exit.");
    println!();

    loop {
        let t = rtc::read();
        print!(
            "\r{:04}-{:02}-{:02}  {:02}:{:02}:{:02}   ",
            t.year, t.month, t.day, t.hours, t.minutes, t.seconds
        );

        // Non-blocking check now that read_scancode() genuinely blocks
        // for a real event -- this keeps the clock ticking every loop
        // instead of freezing until a keypress arrives.
        if keyboard::try_read_scancode().is_some() {
            println!();
            println!("Closing Clock.");
            return;
        }

        for _ in 0..2_000_000 {
            unsafe { core::arch::asm!("nop") };
        }
    }
}