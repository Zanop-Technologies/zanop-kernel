#include "../../zanop-desktop-os/compositor/src/surface.hpp"
#include "drivers/vga.hpp"
#include "framebuffer/framebuffer.hpp"
// NOTE: relative path above assumes zanop-desktop-os and zanop-kernel
// sit as sibling folders. Adjust if your local layout differs.

extern "C" void* memcpy(void* destination, const void* source, std::size_t count) {
    auto* dst = static_cast<unsigned char*>(destination);
    auto* src = static_cast<const unsigned char*>(source);
    for (std::size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
    return destination;
}

namespace surface {

namespace {

constexpr std::uint8_t FONT[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
    {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e},
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c},
    {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01},
    {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06},
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f},
    {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}
};

const std::uint8_t* glyph_for(char character) {
    static constexpr std::uint8_t blank[5] = {};
    static constexpr std::uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static constexpr std::uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static constexpr std::uint8_t period[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static constexpr std::uint8_t slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
    static constexpr std::uint8_t plus[5] = {0x08, 0x08, 0x3e, 0x08, 0x08};
    static constexpr std::uint8_t star[5] = {0x14, 0x08, 0x3e, 0x08, 0x14};
    static constexpr std::uint8_t equals[5] = {0x14, 0x14, 0x14, 0x14, 0x14};
    static constexpr std::uint8_t underscore[5] = {0x40, 0x40, 0x40, 0x40, 0x40};
    static constexpr std::uint8_t bang[5] = {0x00, 0x00, 0x5f, 0x00, 0x00};
    static constexpr std::uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
    static constexpr std::uint8_t comma[5] = {0x00, 0x60, 0x38, 0x00, 0x00};
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if (character >= '0' && character <= '9') return FONT[character - '0'];
    if (character >= 'A' && character <= 'Z') return FONT[10 + character - 'A'];
    if (character == '-') return dash;
    if (character == ':') return colon;
    if (character == '.') return period;
    if (character == '/') return slash;
    if (character == '+') return plus;
    if (character == '*') return star;
    if (character == '=') return equals;
    if (character == '_') return underscore;
    if (character == '!') return bang;
    if (character == '?') return question;
    if (character == ',') return comma;
    return blank;
}

} // namespace

bool graphics_available() {
    return Framebuffer::is_available();
}

std::size_t pixel_width() {
    return Framebuffer::width();
}

std::size_t pixel_height() {
    return Framebuffer::height();
}

void draw_pixel(std::size_t x, std::size_t y, Color color) {
    if (!graphics_available() || x >= pixel_width() || y >= pixel_height()) return;
    Framebuffer::put_pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                           color.red, color.green, color.blue);
}

std::uint32_t read_pixel_raw(std::size_t x, std::size_t y) {
    if (!graphics_available() || x >= pixel_width() || y >= pixel_height()) return 0;
    return Framebuffer::read_pixel_raw(static_cast<std::uint32_t>(x),
                                      static_cast<std::uint32_t>(y));
}

void draw_pixel_raw(std::size_t x, std::size_t y, std::uint32_t pixel) {
    if (!graphics_available() || x >= pixel_width() || y >= pixel_height()) return;
    Framebuffer::put_pixel_raw(static_cast<std::uint32_t>(x),
                               static_cast<std::uint32_t>(y), pixel);
}

void fill_pixel_rect(std::size_t x, std::size_t y, std::size_t width,
                     std::size_t height, Color color) {
    if (!graphics_available() || x >= pixel_width() || y >= pixel_height()) return;
    const std::size_t clipped_width = width < pixel_width() - x ? width : pixel_width() - x;
    const std::size_t clipped_height = height < pixel_height() - y ? height : pixel_height() - y;
    Framebuffer::fill_rect(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                           static_cast<std::uint32_t>(clipped_width),
                           static_cast<std::uint32_t>(clipped_height),
                           color.red, color.green, color.blue);
}

void draw_rgb_bitmap(std::size_t x, std::size_t y, std::size_t width,
                     std::size_t height, const std::uint8_t* pixels) {
    if (!graphics_available() || pixels == nullptr) return;
    Framebuffer::draw_rgb_bitmap(static_cast<std::uint32_t>(x),
                                 static_cast<std::uint32_t>(y),
                                 static_cast<std::uint32_t>(width),
                                 static_cast<std::uint32_t>(height), pixels);
}

namespace {
void (*present_hook)() = nullptr;
bool in_present_hook = false;

// Runs the hook after a flip. The hook may itself flip (present_rect), so it
// is not re-entered.
void run_present_hook() {
    if (present_hook == nullptr || in_present_hook) return;
    in_present_hook = true;
    present_hook();
    in_present_hook = false;
}
} // namespace

void set_present_hook(void (*hook)()) {
    present_hook = hook;
}

void present() {
    Framebuffer::present();
    run_present_hook();
}

void present_rect(std::size_t x, std::size_t y, std::size_t width, std::size_t height) {
    Framebuffer::present_rect(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                              static_cast<std::uint32_t>(width),
                              static_cast<std::uint32_t>(height));
    run_present_hook();
}

void draw_text(std::size_t x, std::size_t y, const char* text, Color color,
               std::size_t scale, Font font) {
    (void)font;
    if (!graphics_available() || text == nullptr || scale == 0) return;
    for (std::size_t index = 0; text[index] != '\0'; ++index) {
        const std::uint8_t* glyph = glyph_for(text[index]);
        for (std::size_t column = 0; column < 5; ++column) {
            for (std::size_t row = 0; row < 7; ++row) {
                if ((glyph[column] & (1u << row)) != 0) {
                    fill_pixel_rect(x + index * 6 * scale + column * scale,
                                    y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void put_char(std::size_t col, std::size_t row, char c) {
    if (col >= SCREEN_WIDTH || row >= SCREEN_HEIGHT) return;
    VGA::writer.put_at(col, row, c);
}

void put_str(std::size_t col, std::size_t row, const char* s) {
    put_str_clipped(col, row, s, SCREEN_WIDTH - col);
}

void put_str_clipped(std::size_t col, std::size_t row, const char* s, std::size_t max_width) {
    if (col >= SCREEN_WIDTH || row >= SCREEN_HEIGHT || s == nullptr) return;
    std::size_t i = 0;
    while (s[i] != '\0' && i < max_width && col + i < SCREEN_WIDTH) {
        put_char(col + i, row, s[i]);
        ++i;
    }
}

void fill_rect(std::size_t col, std::size_t row, std::size_t w, std::size_t h, char c) {
    std::size_t max_row = row + h < SCREEN_HEIGHT ? row + h : SCREEN_HEIGHT;
    std::size_t max_col = col + w < SCREEN_WIDTH ? col + w : SCREEN_WIDTH;
    for (std::size_t y = row; y < max_row; ++y) {
        for (std::size_t x = col; x < max_col; ++x) {
            put_char(x, y, c);
        }
    }
}

} // namespace surface