#pragma once

#include "../types.hpp"

namespace Drivers {
namespace ATA {

struct DeviceInfo {
    std::uint64_t sectors;
    char model[41];
    std::uint8_t channel;
    std::uint8_t unit;
};

void init();
std::size_t device_count();
const DeviceInfo* device_info(std::size_t index);
bool read_sectors(std::size_t device, std::uint32_t lba, std::uint8_t count,
                  std::uint8_t* buffer);
bool write_sectors(std::size_t device, std::uint32_t lba, std::uint8_t count,
                   const std::uint8_t* buffer);

} // namespace ATA
} // namespace Drivers