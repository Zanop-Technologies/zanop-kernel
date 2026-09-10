#pragma once
#include <cstdint>
#include <cstddef>

namespace VGA {

constexpr std::size_t WIDTH = 80;
constexpr std::size_t HEIGHT = 25;
constexpr std::uintptr_t BUFFER_ADDR = 0xB8000;

enum class Color : std::uint8_t {
    Black = 0, Blue = 1, Green = 2, Cyan = 3, Red = 4,
    Magenta = 5, Brown = 6, LightGray = 7, DarkGray = 8,
    LightBlue = 9, LightGreen = 10, LightCyan = 11, LightRed = 12,
    Pink = 13, Yellow = 14, White = 15,
};

struct ScreenChar {
    std::uint8_t ascii_char;
    std::uint8_t color_code;
};

class Writer {
public:
    Writer();
    void write_string(const char* s);
    void write_byte(char c);
    void clear_screen();

private:
    std::size_t row_ = 0;
    std::size_t col_ = 0;
    std::uint8_t color_code_;
    volatile ScreenChar* buffer_;

    void new_line();
    void clear_row(std::size_t row);
};

extern Writer writer;

// Convenience free functions -- what main.cpp actually calls. Thin
// wrappers around `writer`, kept separate so code that wants direct
// Writer access (e.g. Calculator/Clock/Snake's number formatting)
// still can.
void init();
void print(const char* s);
void println(const char* s);
void write_uint_padded(unsigned value, int width);

} // namespace VGA