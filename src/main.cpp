#include "drivers/vga.hpp"
#include "drivers/keyboard.hpp"
#include "drivers/mouse.hpp"
#include "drivers/ata.hpp"
#include "drivers/serial.hpp"
#include "arch/x86_64/pic.hpp"
#include "arch/x86_64/idt.hpp"
#include "memory/allocator.hpp"
#include "framebuffer/framebuffer.hpp"
#include "framebuffer/multiboot.hpp"
#include "fs/fs.hpp"
#include "panic.hpp"
#include "../../zanop-desktop-os/installer/src/installer.hpp"
#include "../../zanop-desktop-os/compositor/src/surface.hpp"
#include "../../zanop-desktop-os/shell/src/mod.hpp"

namespace {

void draw_boot_progress(bool graphics, std::size_t percent, const char* status) {
    static bool text_initialized = false;
    static bool graphics_initialized = false;

    if (graphics) {
        const std::size_t width = surface::pixel_width();
        const std::size_t height = surface::pixel_height();
        const std::size_t left = width / 8;
        const std::size_t bar_width = width * 3 / 4;
        const std::size_t bar_y = height / 2;
        if (!graphics_initialized) {
            surface::fill_pixel_rect(0, 0, width, height, {12, 24, 36});
            surface::fill_pixel_rect(0, 0, width, 6, {83, 220, 206});
            surface::draw_text(left, bar_y - 76, "ZANOP DESKTOP OS", {238, 245, 248}, 2);
            graphics_initialized = true;
        }
        surface::fill_pixel_rect(left, bar_y - 28, bar_width, 14, {12, 24, 36});
        surface::draw_text(left, bar_y - 28, status, {165, 184, 193}, 1);
        surface::fill_pixel_rect(left, bar_y, bar_width, 20, {50, 69, 78});
        surface::fill_pixel_rect(left, bar_y, bar_width * percent / 100, 20,
                                 {83, 220, 206});
        return;
    }

    if (!text_initialized) {
        VGA::writer.clear_screen();
        VGA::writer.put_at(2, 3, 'Z');
        VGA::writer.put_at(3, 3, 'A');
        VGA::writer.put_at(4, 3, 'N');
        VGA::writer.put_at(5, 3, 'O');
        VGA::writer.put_at(6, 3, 'P');
        VGA::writer.put_at(8, 3, 'D');
        VGA::writer.put_at(9, 3, 'E');
        VGA::writer.put_at(10, 3, 'S');
        VGA::writer.put_at(11, 3, 'K');
        VGA::writer.put_at(12, 3, 'T');
        VGA::writer.put_at(13, 3, 'O');
        VGA::writer.put_at(14, 3, 'P');
        VGA::writer.put_at(16, 3, 'O');
        VGA::writer.put_at(17, 3, 'S');
        text_initialized = true;
    }
    for (std::size_t column = 0; column < 60; ++column) {
        VGA::writer.put_at(column + 10, 10, ' ');
        VGA::writer.put_at(column + 10, 12,
                           column < 60 * percent / 100 ? '#' : '-');
    }
    for (std::size_t index = 0; status[index] != '\0' && index < 60; ++index) {
        VGA::writer.put_at(index + 10, 10, status[index]);
    }
}

} // namespace

extern "C" {

// NOTE: signature changed -- now receives the Multiboot2 info pointer
// boot.asm passes in RDI (this is what the "mov edi, ebx" at the very
// start of _start ends up as by the time we're in 64-bit mode). This
// is REQUIRED for Framebuffer::init() to find the framebuffer tag --
// without this parameter, there's no way to locate it.
void kernel_main(std::uint64_t multiboot_info_addr) {
    serial::init();
    VGA::init();
    draw_boot_progress(false, 8, "SERIAL CONSOLE READY");

    Memory::init();
    draw_boot_progress(false, 22, "MEMORY READY");
    Arch::init();
    draw_boot_progress(false, 38, "INTERRUPTS READY");
    Drivers::Keyboard::init();
    draw_boot_progress(false, 50, "KEYBOARD READY");
    Drivers::Mouse::init();
    draw_boot_progress(false, 62, "MOUSE READY");
    Drivers::ATA::init();
    draw_boot_progress(false, 74, "DISK READY");
    FS::init();
    draw_boot_progress(false, 84, "FILESYSTEM READY");

    bool has_gfx = Framebuffer::init(multiboot_info_addr);
    installer::set_system_memory(multiboot::usable_memory_bytes(multiboot_info_addr));
    const auto payload = multiboot::find_module(multiboot_info_addr, "ZANOP_PAYLOAD");
    if (payload) installer::set_boot_payload((*payload).data, (*payload).size);
    if (has_gfx) {
        draw_boot_progress(true, 92, "FRAMEBUFFER READY");
        // width()/height() reflect whatever was ACTUALLY granted --
        // may not be exactly 800x600 if that mode wasn't available.
    } else {
        draw_boot_progress(false, 92, "VGA TEXT MODE READY");
    }

    draw_boot_progress(has_gfx, 100, "STARTING DESKTOP");
    desktop_shell::run();

    while (true) {
        __asm__ volatile("hlt");
    }
}

void panic(const PanicInfo& info) {
    panic_handler(info);
}

}