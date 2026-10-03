#pragma once
#include "../types.hpp"

// Parses the Multiboot2 info structure passed in RDI at boot. Only
// implements what's needed right now: finding the framebuffer tag.
// Structure per the Multiboot2 spec: 8-byte fixed header
// (total_size, reserved), then a list of tags, each 8-byte aligned,
// each starting with (type: u32, size: u32).

namespace multiboot {

struct FramebufferInfo {
    std::uint64_t addr;
    std::uint32_t pitch;   // bytes per row
    std::uint32_t width;   // pixels
    std::uint32_t height;  // pixels
    std::uint8_t bpp;      // bits per pixel
    // Color field layout -- READ from the tag, never assumed, since
    // this varies by hardware/bootloader.
    std::uint8_t red_pos, red_size;
    std::uint8_t green_pos, green_size;
    std::uint8_t blue_pos, blue_size;
};

struct ModuleInfo {
    const std::uint8_t* data;
    std::size_t size;
};

std::optional<FramebufferInfo> find_framebuffer(std::uint64_t multiboot_info_addr);
std::optional<ModuleInfo> find_module(std::uint64_t multiboot_info_addr,
                                      const char* command_line);
std::uint64_t usable_memory_bytes(std::uint64_t multiboot_info_addr);

} // namespace multiboot