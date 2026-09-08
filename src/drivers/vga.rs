use core::fmt;
use volatile::Volatile;

const VGA_WIDTH: usize = 80;
const VGA_HEIGHT: usize = 25;
const VGA_BUFFER_ADDR: usize = 0xB8000;

#[repr(u8)]
#[allow(dead_code)]
pub enum Color {
    Black = 0, Blue = 1, Green = 2, Cyan = 3, Red = 4,
    Magenta = 5, Brown = 6, LightGray = 7, DarkGray = 8,
    LightBlue = 9, LightGreen = 10, LightCyan = 11, LightRed = 12,
    Pink = 13, Yellow = 14, White = 15,
}

#[derive(Clone, Copy)]
#[repr(transparent)]
struct ColorCode(u8);

impl ColorCode {
    fn new(fg: Color, bg: Color) -> ColorCode {
        ColorCode((bg as u8) << 4 | (fg as u8))
    }
}

#[derive(Clone, Copy)]
#[repr(C)]
struct ScreenChar {
    ascii_char: u8,
    color_code: ColorCode,
}

#[repr(transparent)]
struct Buffer {
    chars: [[Volatile<ScreenChar>; VGA_WIDTH]; VGA_HEIGHT],
}

pub struct Writer {
    col: usize,
    color_code: ColorCode,
    buffer: &'static mut Buffer,
}

impl Writer {
    pub fn write_byte(&mut self, byte: u8) {
        match byte {
            b'\n' => self.new_line(),
            byte => {
                if self.col >= VGA_WIDTH {
                    self.new_line();
                }
                let row = VGA_HEIGHT - 1;
                let col = self.col;
                self.buffer.chars[row][col].write(ScreenChar {
                    ascii_char: byte,
                    color_code: self.color_code,
                });
                self.col += 1;
            }
        }
    }

    pub fn write_string(&mut self, s: &str) {
        for byte in s.bytes() {
            match byte {
                0x20..=0x7e | b'\n' => self.write_byte(byte),
                _ => self.write_byte(0xfe), // unprintable -> block char
            }
        }
    }

    fn new_line(&mut self) {
        for row in 1..VGA_HEIGHT {
            for col in 0..VGA_WIDTH {
                let ch = self.buffer.chars[row][col].read();
                self.buffer.chars[row - 1][col].write(ch);
            }
        }
        self.clear_row(VGA_HEIGHT - 1);
        self.col = 0;
    }

    fn clear_row(&mut self, row: usize) {
        let blank = ScreenChar { ascii_char: b' ', color_code: self.color_code };
        for col in 0..VGA_WIDTH {
            self.buffer.chars[row][col].write(blank);
        }
    }
}

impl fmt::Write for Writer {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.write_string(s);
        Ok(())
    }
}

pub fn init() {
    // writer is constructed lazily via print! macro (see below)
}

pub fn writer() -> Writer {
    Writer {
        col: 0,
        color_code: ColorCode::new(Color::LightGray, Color::Black),
        buffer: unsafe { &mut *(VGA_BUFFER_ADDR as *mut Buffer) },
    }
}