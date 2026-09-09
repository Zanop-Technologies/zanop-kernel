//! Minimal CMOS Real-Time Clock reader.
//! Reads wall-clock time directly via port I/O (0x70/0x71) — no interrupts
//! required, which is why this works even before IDT/IRQ wiring is done.

const CMOS_ADDRESS: u16 = 0x70;
const CMOS_DATA: u16 = 0x71;

pub struct RtcTime {
    pub seconds: u8,
    pub minutes: u8,
    pub hours: u8,
    pub day: u8,
    pub month: u8,
    pub year: u16,
}

unsafe fn cmos_read(reg: u8) -> u8 {
    outb(CMOS_ADDRESS, reg);
    inb(CMOS_DATA)
}

unsafe fn outb(port: u16, val: u8) {
    core::arch::asm!("out dx, al", in("dx") port, in("al") val);
}

unsafe fn inb(port: u16) -> u8 {
    let val: u8;
    core::arch::asm!("in al, dx", in("dx") port, out("al") val);
    val
}

unsafe fn update_in_progress() -> bool {
    outb(CMOS_ADDRESS, 0x0A);
    inb(CMOS_DATA) & 0x80 != 0
}

fn bcd_to_bin(v: u8) -> u8 {
    (v & 0x0F) + ((v / 16) * 10)
}

/// Reads the current time from CMOS. Busy-waits for any in-progress RTC
/// update to finish first, and reads twice to guard against a value
/// changing mid-read (standard CMOS RTC read pattern).
pub fn read() -> RtcTime {
    unsafe {
        while update_in_progress() {}

        let mut second = cmos_read(0x00);
        let mut minute = cmos_read(0x02);
        let mut hour = cmos_read(0x04);
        let mut day = cmos_read(0x07);
        let mut month = cmos_read(0x08);
        let mut year = cmos_read(0x09);

        loop {
            while update_in_progress() {}
            let second2 = cmos_read(0x00);
            let minute2 = cmos_read(0x02);
            let hour2 = cmos_read(0x04);
            let day2 = cmos_read(0x07);
            let month2 = cmos_read(0x08);
            let year2 = cmos_read(0x09);

            if second == second2 && minute == minute2 && hour == hour2
                && day == day2 && month == month2 && year == year2 {
                break;
            }
            second = second2; minute = minute2; hour = hour2;
            day = day2; month = month2; year = year2;
        }

        let status_b = cmos_read(0x0B);
        let is_binary = status_b & 0x04 != 0;
        let is_24h = status_b & 0x02 != 0;

        if !is_binary {
            second = bcd_to_bin(second);
            minute = bcd_to_bin(minute);
            hour = bcd_to_bin(hour & 0x7F) | (hour & 0x80);
            day = bcd_to_bin(day);
            month = bcd_to_bin(month);
            year = bcd_to_bin(year);
        }

        if !is_24h && (hour & 0x80) != 0 {
            hour = ((hour & 0x7F) + 12) % 24;
        }

        RtcTime {
            seconds: second,
            minutes: minute,
            hours: hour,
            day,
            month,
            year: 2000 + year as u16, // assumes 21st century
        }
    }
}