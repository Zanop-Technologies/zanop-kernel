#include "framebuffer.hpp"

namespace Framebuffer {
namespace {
multiboot::FramebufferInfo info{};
bool available = false;

std::uint32_t pack_color(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    // Shifts each channel into its ACTUAL reported bit position --
    // this is what makes the driver work across different hardware/
    // bootloader color layouts instead of assuming one fixed format.
    std::uint32_t rv = (static_cast<std::uint32_t>(r) >> (8 - info.red_size)) << info.red_pos;
    std::uint32_t gv = (static_cast<std::uint32_t>(g) >> (8 - info.green_size)) << info.green_pos;
    std::uint32_t bv = (static_cast<std::uint32_t>(b) >> (8 - info.blue_size)) << info.blue_pos;
    return rv | gv | bv;
}
}

bool init(std::uint64_t multiboot_info_addr) {
    auto fb = multiboot::find_framebuffer(multiboot_info_addr);
    if (!fb) {
        available = false;
        return false;
    }
    info = *fb;

    // Only 32bpp is actually supported by put_pixel below -- flagging
    // rather than silently misdrawing if a bootloader ever grants a
    // different depth than requested (boot.asm requests 32, but
    // Multiboot2 allows the bootloader to grant something else if
    // that exact mode isn't available).
    if (info.bpp != 32) {
        available = false;
        return false;
    }

    available = true;
    return true;
}

bool is_available() { return available; }
std::uint32_t width() { return info.width; }
std::uint32_t height() { return info.height; }

void put_pixel(std::uint32_t x, std::uint32_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (!available || x >= info.width || y >= info.height) return;

    std::uint8_t* row = reinterpret_cast<std::uint8_t*>(info.addr) + y * info.pitch;
    std::uint32_t* pixel = reinterpret_cast<std::uint32_t*>(row) + x;
    *pixel = pack_color(r, g, b);
}

std::uint32_t read_pixel_raw(std::uint32_t x, std::uint32_t y) {
    if (!available || x >= info.width || y >= info.height) return 0;
    std::uint8_t* row = reinterpret_cast<std::uint8_t*>(info.addr) + y * info.pitch;
    return reinterpret_cast<std::uint32_t*>(row)[x];
}

void put_pixel_raw(std::uint32_t x, std::uint32_t y, std::uint32_t value) {
    if (!available || x >= info.width || y >= info.height) return;
    std::uint8_t* row = reinterpret_cast<std::uint8_t*>(info.addr) + y * info.pitch;
    reinterpret_cast<std::uint32_t*>(row)[x] = value;
}

void fill_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    for (std::uint32_t row = y; row < y + h; ++row) {
        for (std::uint32_t col = x; col < x + w; ++col) {
            put_pixel(col, row, r, g, b);
        }
    }
}

void draw_rgb_bitmap(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                       const std::uint8_t* rgb_data) {
    for (std::uint32_t row = 0; row < h; ++row) {
        for (std::uint32_t col = 0; col < w; ++col) {
            std::size_t idx = (static_cast<std::size_t>(row) * w + col) * 3;
            put_pixel(x + col, y + row, rgb_data[idx], rgb_data[idx + 1], rgb_data[idx + 2]);
        }
    }
}

} // namespace Framebuffer