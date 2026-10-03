#include "multiboot.hpp"

namespace multiboot {
namespace {
constexpr std::uint32_t TAG_FRAMEBUFFER = 8;
constexpr std::uint32_t TAG_MODULE = 3;
constexpr std::uint32_t TAG_MEMORY_MAP = 6;
constexpr std::uint32_t TAG_END = 0;

template <typename T>
T read(const std::uint8_t* p) {
    T value;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        reinterpret_cast<std::uint8_t*>(&value)[i] = p[i];
    }
    return value;
}
}

std::optional<FramebufferInfo> find_framebuffer(std::uint64_t multiboot_info_addr) {
    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(multiboot_info_addr);
    std::uint32_t total_size = read<std::uint32_t>(base);

    std::uint32_t offset = 8; // skip fixed header (total_size, reserved)

    while (offset < total_size) {
        const std::uint8_t* tag = base + offset;
        std::uint32_t type = read<std::uint32_t>(tag);
        std::uint32_t size = read<std::uint32_t>(tag + 4);

        if (type == TAG_END) break;

        if (type == TAG_FRAMEBUFFER) {
            FramebufferInfo info;
            info.addr = read<std::uint64_t>(tag + 8);
            info.pitch = read<std::uint32_t>(tag + 16);
            info.width = read<std::uint32_t>(tag + 20);
            info.height = read<std::uint32_t>(tag + 24);
            info.bpp = *(tag + 28);
            std::uint8_t fb_type = *(tag + 29);

            if (fb_type == 1) { // RGB direct color -- read the real field layout
                info.red_pos = *(tag + 32);
                info.red_size = *(tag + 33);
                info.green_pos = *(tag + 34);
                info.green_size = *(tag + 35);
                info.blue_pos = *(tag + 36);
                info.blue_size = *(tag + 37);
            } else {
                // Indexed/EGA text framebuffer types aren't handled --
                // only RGB direct color is supported right now.
                return std::nullopt;
            }

            return info;
        }

        // tags are 8-byte aligned
        offset += (size + 7) & ~7u;
    }

    return std::nullopt;
}

std::optional<ModuleInfo> find_module(std::uint64_t multiboot_info_addr,
                                      const char* command_line) {
    if (multiboot_info_addr == 0 || command_line == nullptr) return std::nullopt;
    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(multiboot_info_addr);
    const std::uint32_t total_size = read<std::uint32_t>(base);
    if (total_size < 16) return std::nullopt;

    std::uint32_t offset = 8;
    while (offset <= total_size - 8) {
        const std::uint8_t* tag = base + offset;
        const std::uint32_t type = read<std::uint32_t>(tag);
        const std::uint32_t size = read<std::uint32_t>(tag + 4);
        if (size < 8 || size > total_size - offset) return std::nullopt;
        if (type == TAG_END) break;

        if (type == TAG_MODULE && size >= 17) {
            const char* module_command = reinterpret_cast<const char*>(tag + 16);
            const std::size_t command_capacity = size - 16;
            std::size_t index = 0;
            while (index < command_capacity && module_command[index] != '\0' &&
                   command_line[index] != '\0' && module_command[index] == command_line[index]) {
                ++index;
            }
            if (index < command_capacity && module_command[index] == '\0' &&
                command_line[index] == '\0') {
                const std::uint32_t start = read<std::uint32_t>(tag + 8);
                const std::uint32_t end = read<std::uint32_t>(tag + 12);
                if (start == 0 || end <= start) return std::nullopt;
                return ModuleInfo{reinterpret_cast<const std::uint8_t*>(start), end - start};
            }
        }

        const std::uint32_t aligned_size = (size + 7) & ~7u;
        if (aligned_size > total_size - offset) return std::nullopt;
        offset += aligned_size;
    }
    return std::nullopt;
}

std::uint64_t usable_memory_bytes(std::uint64_t multiboot_info_addr) {
    if (multiboot_info_addr == 0) return 0;
    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(multiboot_info_addr);
    const std::uint32_t total_size = read<std::uint32_t>(base);
    if (total_size < 16) return 0;

    std::uint32_t offset = 8;
    while (offset <= total_size - 8) {
        const std::uint8_t* tag = base + offset;
        const std::uint32_t type = read<std::uint32_t>(tag);
        const std::uint32_t size = read<std::uint32_t>(tag + 4);
        if (size < 8 || size > total_size - offset) return 0;
        if (type == TAG_END) break;

        if (type == TAG_MEMORY_MAP && size >= 16) {
            const std::uint32_t entry_size = read<std::uint32_t>(tag + 8);
            if (entry_size < 24 || entry_size > size - 16) return 0;
            std::uint64_t usable = 0;
            std::uint32_t entry_offset = 16;
            while (entry_offset <= size - entry_size) {
                const std::uint8_t* entry = tag + entry_offset;
                const std::uint64_t address = read<std::uint64_t>(entry);
                const std::uint64_t length = read<std::uint64_t>(entry + 8);
                const std::uint32_t entry_type = read<std::uint32_t>(entry + 16);
                if (entry_type == 1 && address <= 0xFFFFFFFFFFFFFFFFull - length) {
                    if (usable > 0xFFFFFFFFFFFFFFFFull - length) {
                        return 0xFFFFFFFFFFFFFFFFull;
                    }
                    usable += length;
                }
                entry_offset += entry_size;
            }
            return usable;
        }

        const std::uint32_t aligned_size = (size + 7) & ~7u;
        if (aligned_size > total_size - offset) return 0;
        offset += aligned_size;
    }
    return 0;
}

} // namespace multiboot