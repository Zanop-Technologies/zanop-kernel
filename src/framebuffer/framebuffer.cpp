#include "framebuffer.hpp"

namespace Framebuffer {
namespace {
multiboot::FramebufferInfo info{};
bool available = false;

// Back buffer. Sits in .bss (NOLOAD in the linker script, so it costs
// nothing in the kernel image or the ISO -- only RAM at runtime).
//
// The cap matters: a 4K frame is ~33 MB, and if the bootloader hands us
// more framebuffer than this we simply stay single-buffered rather than
// grabbing an unreasonable chunk of RAM. 16 MB covers everything up to
// 2048x2048, which is well past what any of this code asks for.
constexpr std::size_t BACK_BUFFER_BYTES = 16u * 1024u * 1024u;
alignas(4096) std::uint8_t back_buffer[BACK_BUFFER_BYTES];
std::uint8_t* back_buffer_used = nullptr;
std::size_t back_buffer_size = 0;
bool double_buffered = false;

// Where every drawing call actually writes: the back buffer when double
// buffering is on, otherwise the display itself.
std::uint8_t* draw_surface() {
    return back_buffer_used != nullptr ? back_buffer_used
                                       : reinterpret_cast<std::uint8_t*>(info.addr);
}


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

    std::uint8_t* row = draw_surface() + y * info.pitch;
    std::uint32_t* pixel = reinterpret_cast<std::uint32_t*>(row) + x;
    *pixel = pack_color(r, g, b);
}

std::uint32_t read_pixel_raw(std::uint32_t x, std::uint32_t y) {
    if (!available || x >= info.width || y >= info.height) return 0;
    std::uint8_t* row = draw_surface() + y * info.pitch;
    return reinterpret_cast<std::uint32_t*>(row)[x];
}

void put_pixel_raw(std::uint32_t x, std::uint32_t y, std::uint32_t value) {
    if (!available || x >= info.width || y >= info.height) return;
    std::uint8_t* row = draw_surface() + y * info.pitch;
    reinterpret_cast<std::uint32_t*>(row)[x] = value;
}
bool enable_double_buffer() {
    if (!available) return false;
    const std::size_t bytes = static_cast<std::size_t>(info.pitch) * info.height;
    if (bytes == 0 || bytes > BACK_BUFFER_BYTES) {
        // Too big to back (a 4K mode, say). Stay single-buffered: identical
        // pixels on screen, just no flicker suppression.
        double_buffered = false;
        back_buffer_used = nullptr;
        back_buffer_size = 0;
        return false;
    }
    back_buffer_size = bytes;
    back_buffer_used = back_buffer;
    double_buffered = true;
    return true;
}
bool is_double_buffered() { return double_buffered; }
void present() {
    if (!double_buffered || back_buffer_used == nullptr || !available) return;
    // Copy a word at a time rather than a byte at a time. pitch can exceed
    // width*4 (some modes are padded), so this still walks row by row --
    // but each step moves 8 bytes instead of 1, which is what makes the
    // flip cheap enough to do every frame.
    std::uint8_t* display = reinterpret_cast<std::uint8_t*>(info.addr);
    const std::size_t row_bytes = static_cast<std::size_t>(info.width) * 4;
    const std::size_t words = row_bytes / sizeof(std::uint64_t);
    for (std::uint32_t y = 0; y < info.height; ++y) {
        std::uint8_t* dst = display + y * info.pitch;
        const std::uint8_t* src = back_buffer_used + y * info.pitch;
        auto* dst_words = reinterpret_cast<std::uint64_t*>(dst);
        const auto* src_words = reinterpret_cast<const std::uint64_t*>(src);
        for (std::size_t i = 0; i < words; ++i) dst_words[i] = src_words[i];
        for (std::size_t i = words * sizeof(std::uint64_t); i < row_bytes; ++i) {
            dst[i] = src[i];
        }
    }
}
void present_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    if (!double_buffered || back_buffer_used == nullptr || !available) return;
    if (x >= info.width || y >= info.height) return;
    if (w > info.width - x) w = info.width - x;
    if (h > info.height - y) h = info.height - y;
    if (w == 0 || h == 0) return;
    for (std::uint32_t row = y; row < y + h; ++row) {
        auto* dst = reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(info.addr) + row * info.pitch) + x;
        const auto* src = reinterpret_cast<const std::uint32_t*>(
            back_buffer_used + row * info.pitch) + x;
        for (std::uint32_t i = 0; i < w; ++i) dst[i] = src[i];
    }
}
void fill_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (!available) return;
    // Clip once here and then write whole rows straight into the surface.
    // The old per-pixel loop paid a bounds check plus a colour repack for
    // every single pixel; a solid rectangle is the single most common draw
    // call in the whole UI, so this is where the frame time really went.
    if (x >= info.width || y >= info.height) return;
    if (x + w > info.width) w = info.width - x;
    if (y + h > info.height) h = info.height - y;
    if (w == 0 || h == 0) return;
    const std::uint32_t pixel = pack_color(r, g, b);
    std::uint8_t* surface = draw_surface();
    for (std::uint32_t row = y; row < y + h; ++row) {
        auto* destination = reinterpret_cast<std::uint32_t*>(surface + row * info.pitch) + x;
        // Widen to 64-bit so the compiler can emit a vectorised store loop.
        std::uint64_t wide = ((static_cast<std::uint64_t>(pixel) << 32) | pixel);
        std::uint32_t remaining = w;
        while (remaining >= 2) {
            *reinterpret_cast<std::uint64_t*>(destination) = wide;
            destination += 2;
            remaining -= 2;
        }
        if (remaining != 0) *destination = pixel;
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