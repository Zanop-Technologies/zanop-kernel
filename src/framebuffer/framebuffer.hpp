#pragma once
#include "../types.hpp"
#include "multiboot.hpp"

namespace Framebuffer {

// Call once at boot with the multiboot info pointer RDI carried in
// from boot.asm. Returns false if no usable framebuffer was found
// (caller should fall back to VGA text mode in that case).
bool init(std::uint64_t multiboot_info_addr);

bool is_available();

void put_pixel(std::uint32_t x, std::uint32_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b);
std::uint32_t read_pixel_raw(std::uint32_t x, std::uint32_t y);
void put_pixel_raw(std::uint32_t x, std::uint32_t y, std::uint32_t pixel);
void fill_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                std::uint8_t r, std::uint8_t g, std::uint8_t b);

// Draws a packed RGB888 (3 bytes per pixel, row-major, no padding)
// bitmap at (x, y). Used for embedded photo data -- see
// tools/photo_to_bitmap.py for how that data gets generated.
void draw_rgb_bitmap(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                       const std::uint8_t* rgb_data);

std::uint32_t width();
std::uint32_t height();

} // namespace Framebuffer