#include "ata.hpp"

namespace {

constexpr std::uint16_t DATA = 0;
constexpr std::uint16_t SECTOR_COUNT = 2;
constexpr std::uint16_t LBA_LOW = 3;
constexpr std::uint16_t LBA_MID = 4;
constexpr std::uint16_t LBA_HIGH = 5;
constexpr std::uint16_t DRIVE = 6;
constexpr std::uint16_t STATUS = 7;
constexpr std::uint16_t COMMAND = 7;
constexpr std::uint8_t STATUS_ERR = 0x01;
constexpr std::uint8_t STATUS_DRQ = 0x08;
constexpr std::uint8_t STATUS_DF = 0x20;
constexpr std::uint8_t STATUS_BSY = 0x80;
constexpr std::size_t MAX_DEVICES = 4;
constexpr std::uint32_t MAX_LBA28 = 0x10000000u;

struct Channel {
    std::uint16_t io;
    std::uint16_t control;
};

struct Device {
    Drivers::ATA::DeviceInfo info;
    Channel channel;
    bool present;
};

Channel channels[2] = {{0x1F0, 0x3F6}, {0x170, 0x376}};
Device devices[MAX_DEVICES] = {};
std::size_t devices_found = 0;

inline void outb(std::uint16_t port, std::uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline std::uint8_t inb(std::uint16_t port) {
    std::uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline std::uint16_t inw(std::uint16_t port) {
    std::uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void outw(std::uint16_t port, std::uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

void delay_400ns(Channel channel) {
    (void)inb(channel.control);
    (void)inb(channel.control);
    (void)inb(channel.control);
    (void)inb(channel.control);
}

bool wait_not_busy(Channel channel, std::uint8_t& status) {
    for (std::uint32_t attempt = 0; attempt < 1000000; ++attempt) {
        status = inb(channel.io + STATUS);
        if (status == 0 || status == 0xFF) return false;
        if ((status & STATUS_BSY) == 0) return true;
        __asm__ volatile("pause");
    }
    return false;
}

bool wait_data_request(Channel channel) {
    std::uint8_t status = 0;
    if (!wait_not_busy(channel, status)) return false;
    return (status & (STATUS_ERR | STATUS_DF)) == 0 && (status & STATUS_DRQ) != 0;
}

bool identify(Channel channel, std::uint8_t unit, Device& result) {
    outb(channel.io + DRIVE, static_cast<std::uint8_t>(0xA0 | (unit << 4)));
    delay_400ns(channel);
    outb(channel.io + SECTOR_COUNT, 0);
    outb(channel.io + LBA_LOW, 0);
    outb(channel.io + LBA_MID, 0);
    outb(channel.io + LBA_HIGH, 0);
    outb(channel.io + COMMAND, 0xEC);

    std::uint8_t status = inb(channel.io + STATUS);
    if (status == 0 || status == 0xFF) return false;
    if (!wait_not_busy(channel, status)) return false;

    if (inb(channel.io + LBA_MID) != 0 || inb(channel.io + LBA_HIGH) != 0) {
        return false;
    }
    if ((status & (STATUS_ERR | STATUS_DF)) != 0 || !wait_data_request(channel)) {
        return false;
    }

    std::uint16_t identify_data[256];
    for (std::size_t index = 0; index < 256; ++index) {
        identify_data[index] = inw(channel.io + DATA);
    }

    std::uint32_t sectors = static_cast<std::uint32_t>(identify_data[60]) |
                            (static_cast<std::uint32_t>(identify_data[61]) << 16);
    if (sectors == 0 || sectors > MAX_LBA28) return false;

    result.info.sectors = sectors;
    result.info.channel = channel.io == channels[0].io ? 0 : 1;
    result.info.unit = unit;
    for (std::size_t index = 0; index < 20; ++index) {
        const std::uint16_t word = identify_data[27 + index];
        result.info.model[index * 2] = static_cast<char>(word >> 8);
        result.info.model[index * 2 + 1] = static_cast<char>(word & 0xFF);
    }
    result.info.model[40] = '\0';
    result.channel = channel;
    result.present = true;
    return true;
}

bool transfer_sectors(Device& device, std::uint32_t lba, std::uint8_t count,
                  std::uint8_t* buffer, bool write) {
    Channel channel = device.channel;
    outb(channel.io + DRIVE, static_cast<std::uint8_t>(0xE0 |
         (device.info.unit << 4) | ((lba >> 24) & 0x0F)));
    delay_400ns(channel);
    outb(channel.io + SECTOR_COUNT, count);
    outb(channel.io + LBA_LOW, static_cast<std::uint8_t>(lba));
    outb(channel.io + LBA_MID, static_cast<std::uint8_t>(lba >> 8));
    outb(channel.io + LBA_HIGH, static_cast<std::uint8_t>(lba >> 16));
    outb(channel.io + COMMAND, write ? 0x30 : 0x20);

    for (std::size_t sector = 0; sector < count; ++sector) {
        if (!wait_data_request(channel)) return false;
        std::uint8_t* sector_buffer = buffer + sector * 512;
        for (std::size_t index = 0; index < 256; ++index) {
            if (write) {
                const std::uint16_t word = static_cast<std::uint16_t>(sector_buffer[index * 2]) |
                    (static_cast<std::uint16_t>(sector_buffer[index * 2 + 1]) << 8);
                outw(channel.io + DATA, word);
            } else {
                const std::uint16_t word = inw(channel.io + DATA);
                sector_buffer[index * 2] = static_cast<std::uint8_t>(word);
                sector_buffer[index * 2 + 1] = static_cast<std::uint8_t>(word >> 8);
            }
        }
    }

    std::uint8_t status = 0;
    if (!wait_not_busy(channel, status) || (status & (STATUS_ERR | STATUS_DF)) != 0) {
        return false;
    }
    if (!write) return true;
    outb(channel.io + COMMAND, 0xE7);
    return wait_not_busy(channel, status) && (status & (STATUS_ERR | STATUS_DF)) == 0;
}

} // namespace

namespace Drivers {
namespace ATA {

void init() {
    devices_found = 0;
    for (Channel channel : channels) {
        outb(channel.control, 0x02);
        for (std::uint8_t unit = 0; unit < 2; ++unit) {
            Device candidate{};
            if (identify(channel, unit, candidate) && devices_found < MAX_DEVICES) {
                devices[devices_found++] = candidate;
            }
        }
    }
}

std::size_t device_count() {
    return devices_found;
}

const DeviceInfo* device_info(std::size_t index) {
    if (index >= devices_found) return nullptr;
    return &devices[index].info;
}

bool read_sectors(std::size_t device_id, std::uint32_t lba, std::uint8_t count,
                  std::uint8_t* buffer) {
    if (device_id >= devices_found || count == 0 || buffer == nullptr) return false;
    Device& device = devices[device_id];
    if (lba >= device.info.sectors || count > device.info.sectors - lba ||
        lba >= MAX_LBA28 || count > MAX_LBA28 - lba) return false;
    return transfer_sectors(device, lba, count, buffer, false);
}

bool write_sectors(std::size_t device_id, std::uint32_t lba, std::uint8_t count,
                   const std::uint8_t* buffer) {
    if (device_id >= devices_found || count == 0 || buffer == nullptr) return false;
    Device& device = devices[device_id];
    if (lba >= device.info.sectors || count > device.info.sectors - lba ||
        lba >= MAX_LBA28 || count > MAX_LBA28 - lba) return false;
    return transfer_sectors(device, lba, count,
        const_cast<std::uint8_t*>(buffer), true);
}

} // namespace ATA
} // namespace Drivers