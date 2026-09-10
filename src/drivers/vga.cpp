#include "vga.hpp"

namespace VGA {

Writer::Writer()
    : color_code_(static_cast<std::uint8_t>(Color::LightGray)),
      buffer_(reinterpret_cast<volatile ScreenChar*>(BUFFER_ADDR)) {}

void Writer::clear_row(std::size_t row) {
    ScreenChar blank{' ', color_code_};
    for (std::size_t col = 0; col < WIDTH; ++col) {
        buffer_[row * WIDTH + col] = blank;
    }
}

void Writer::clear_screen() {
    for (std::size_t row = 0; row < HEIGHT; ++row) clear_row(row);
    row_ = 0;
    col_ = 0;
}

void Writer::new_line() {
    if (row_ < HEIGHT - 1) {
        ++row_;
    } else {
        for (std::size_t row = 1; row < HEIGHT; ++row) {
            for (std::size_t col = 0; col < WIDTH; ++col) {
                buffer_[(row - 1) * WIDTH + col] = buffer_[row * WIDTH + col];
            }
        }
        clear_row(HEIGHT - 1);
    }
    col_ = 0;
}

void Writer::write_byte(char c) {
    if (c == '\n') { new_line(); return; }
    if (col_ >= WIDTH) new_line();
    buffer_[row_ * WIDTH + col_] = ScreenChar{static_cast<std::uint8_t>(c), color_code_};
    ++col_;
}

void Writer::write_string(const char* s) {
    while (*s) write_byte(*s++);
}

Writer writer;

void init() { writer.clear_screen(); }
void print(const char* s) { writer.write_string(s); }
void println(const char* s) { writer.write_string(s); writer.write_byte('\n'); }

void write_uint_padded(unsigned value, int width) {
    char buf[16];
    int i = 0;
    if (value == 0) buf[i++] = '0';
    while (value > 0) { buf[i++] = static_cast<char>('0' + (value % 10)); value /= 10; }
    for (int pad = i; pad < width; ++pad) writer.write_byte('0');
    while (i > 0) writer.write_byte(buf[--i]);
}

} // namespace VGA