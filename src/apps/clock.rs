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

        // Exit on any keypress; otherwise keep refreshing.
        let scancode = keyboard::read_scancode();
        if scancode != 0 {
            println!();
            println!("Closing Clock.");
            return;
        }

        // Small busy-wait so the display doesn't flicker faster than the
        // RTC's 1-second resolution actually changes. Not a precise
        // delay — there's no timer interrupt to base one on yet.
        for _ in 0..2_000_000 {
            unsafe { core::arch::asm!("nop") };
        }
    }
}