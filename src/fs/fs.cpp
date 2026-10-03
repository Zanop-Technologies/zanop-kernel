#include "fs.hpp"
#include "../drivers/vga.hpp"
#include "../drivers/ata.hpp"

namespace {

constexpr size_t MAX_FILES = 16;
constexpr size_t DISK_FILE_COUNT = 64;
constexpr uint32_t DIRECTORY_LBA = 1;
constexpr uint32_t DIRECTORY_SECTORS = 8;
constexpr uint32_t DATA_LBA = DIRECTORY_LBA + DIRECTORY_SECTORS;
constexpr size_t SECTOR_SIZE = 512;
constexpr size_t NAME_SIZE = 48;
constexpr char FILESYSTEM_MAGIC[8] = {'Z', 'A', 'N', 'F', 'S', '0', '1', '\0'};

struct __attribute__((packed)) Superblock {
	char magic[8];
	uint32_t version;
	uint32_t total_sectors;
	uint32_t directory_lba;
	uint32_t directory_sectors;
	uint32_t data_lba;
	uint8_t reserved[484];
};

struct __attribute__((packed)) DiskFile {
	char name[NAME_SIZE];
	uint32_t start_lba;
	uint32_t size_bytes;
	uint32_t sector_count;
	uint8_t reserved[4];
};

static_assert(sizeof(Superblock) == SECTOR_SIZE);
static_assert(sizeof(DiskFile) == SECTOR_SIZE / 8);

FS::File files[MAX_FILES] = {};
DiskFile disk_files[DISK_FILE_COUNT] = {};
uint8_t sector_buffer[SECTOR_SIZE];
char compatibility_buffer[256];
bool disk_mounted = false;
size_t active_device = 0;
uint32_t disk_sector_count = 0;

void clear_bytes(void* memory, size_t size) {
	uint8_t* bytes = static_cast<uint8_t*>(memory);
	for (size_t index = 0; index < size; ++index) bytes[index] = 0;
}

void copy_string(char* destination, size_t capacity, const char* source) {
	if (capacity == 0) return;
	size_t index = 0;
	while (index + 1 < capacity && source[index] != '\0') {
		destination[index] = source[index];
		++index;
	}
	destination[index] = '\0';
}

size_t string_length(const char* text) {
	size_t length = 0;
	while (text[length] != '\0') ++length;
	return length;
}

bool equal_name(const char* left, const char* right) {
	return strcmp(left, right) == 0;
}

int find_disk_file(const char* name) {
	for (size_t index = 0; index < DISK_FILE_COUNT; ++index) {
		if (disk_files[index].name[0] != '\0' && equal_name(disk_files[index].name, name)) {
			return static_cast<int>(index);
		}
	}
	return -1;
}

bool save_directory() {
	return Drivers::ATA::write_sectors(active_device, DIRECTORY_LBA,
		static_cast<uint8_t>(DIRECTORY_SECTORS),
		reinterpret_cast<const uint8_t*>(disk_files));
}

bool load_directory() {
	if (!Drivers::ATA::read_sectors(active_device, DIRECTORY_LBA,
		static_cast<uint8_t>(DIRECTORY_SECTORS),
		reinterpret_cast<uint8_t*>(disk_files))) return false;

	for (size_t index = 0; index < DISK_FILE_COUNT; ++index) {
		const DiskFile& file = disk_files[index];
		if (file.name[0] == '\0') continue;
		bool terminated = false;
		for (size_t character = 0; character < NAME_SIZE; ++character) {
			if (file.name[character] == '\0') {
				terminated = true;
				break;
			}
		}
		if (!terminated || file.start_lba < DATA_LBA ||
			file.start_lba > disk_sector_count ||
			file.sector_count > disk_sector_count - file.start_lba ||
			static_cast<uint64_t>(file.size_bytes) >
				static_cast<uint64_t>(file.sector_count) * SECTOR_SIZE) return false;

		for (size_t other_index = 0; other_index < index; ++other_index) {
			const DiskFile& other = disk_files[other_index];
			if (other.name[0] == '\0' || file.sector_count == 0 ||
				other.sector_count == 0) continue;
			const uint32_t file_end = file.start_lba + file.sector_count;
			const uint32_t other_end = other.start_lba + other.sector_count;
			if (file.start_lba < other_end && other.start_lba < file_end) return false;
		}
	}
	return true;
}

bool write_superblock() {
	Superblock block;
	clear_bytes(&block, sizeof(block));
	for (size_t index = 0; index < sizeof(FILESYSTEM_MAGIC); ++index) {
		block.magic[index] = FILESYSTEM_MAGIC[index];
	}
	block.version = 1;
	block.total_sectors = disk_sector_count;
	block.directory_lba = DIRECTORY_LBA;
	block.directory_sectors = DIRECTORY_SECTORS;
	block.data_lba = DATA_LBA;
	return Drivers::ATA::write_sectors(active_device, 0, 1,
		reinterpret_cast<const uint8_t*>(&block));
}

void cache_name(const char* name) {
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (files[index].used && equal_name(files[index].name, name)) return;
	}
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (!files[index].used) {
			files[index].used = true;
			copy_string(files[index].name, sizeof(files[index].name), name);
			files[index].content[0] = '\0';
			return;
		}
	}
}

uint32_t find_free_extent(uint32_t required, int ignored_entry) {
	uint32_t candidate = DATA_LBA;
	for (size_t pass = 0; pass < DISK_FILE_COUNT; ++pass) {
		int next_entry = -1;
		uint32_t next_start = 0xFFFFFFFFu;
		for (size_t index = 0; index < DISK_FILE_COUNT; ++index) {
			if (static_cast<int>(index) == ignored_entry || disk_files[index].name[0] == '\0') continue;
			const uint32_t start = disk_files[index].start_lba;
			if (disk_files[index].sector_count == 0 || start < candidate) continue;
			if (start < next_start) {
				next_start = start;
				next_entry = static_cast<int>(index);
			}
		}
		if (next_entry < 0 || next_start - candidate >= required) return candidate;
		const DiskFile& file = disk_files[next_entry];
		if (file.sector_count > disk_sector_count - file.start_lba) return 0;
		candidate = file.start_lba + file.sector_count;
	}
	return candidate <= disk_sector_count && required <= disk_sector_count - candidate
		? candidate : 0;
}

bool valid_name(const char* name) {
	if (name == nullptr || name[0] == '\0') return false;
	const size_t length = string_length(name);
	if (length >= NAME_SIZE) return false;
	for (size_t index = 0; index < length; ++index) {
		if (name[index] == '/' || name[index] == '\\') return false;
	}
	return true;
}

} // namespace

namespace FS {

void init() {
	for (size_t index = 0; index < MAX_FILES; ++index) {
		files[index].used = false;
		files[index].content[0] = '\0';
	}
	disk_mounted = false;
	for (size_t index = 0; index < Drivers::ATA::device_count(); ++index) {
		if (mount(index)) break;
	}
}

bool format(size_t device_id) {
	const Drivers::ATA::DeviceInfo* info = Drivers::ATA::device_info(device_id);
	if (info == nullptr || info->sectors <= DATA_LBA || info->sectors > 0xFFFFFFFFu) return false;
	active_device = device_id;
	disk_sector_count = static_cast<uint32_t>(info->sectors);
	disk_mounted = false;
	clear_bytes(disk_files, sizeof(disk_files));
	if (!write_superblock() || !save_directory()) return false;
	disk_mounted = true;
	for (size_t index = 0; index < MAX_FILES; ++index) files[index].used = false;
	return true;
}

bool mount(size_t device_id) {
	const Drivers::ATA::DeviceInfo* info = Drivers::ATA::device_info(device_id);
	if (info == nullptr || info->sectors == 0 || info->sectors > 0xFFFFFFFFu) return false;
	active_device = device_id;
	disk_sector_count = static_cast<uint32_t>(info->sectors);
	disk_mounted = false;
	Superblock block;
	clear_bytes(&block, sizeof(block));
	if (!Drivers::ATA::read_sectors(device_id, 0, 1,
		reinterpret_cast<uint8_t*>(&block))) return false;
	for (size_t index = 0; index < sizeof(FILESYSTEM_MAGIC); ++index) {
		if (block.magic[index] != FILESYSTEM_MAGIC[index]) return false;
	}
	if (block.version != 1 || block.total_sectors <= DATA_LBA ||
		block.total_sectors > disk_sector_count ||
		block.directory_lba != DIRECTORY_LBA ||
		block.directory_sectors != DIRECTORY_SECTORS || block.data_lba != DATA_LBA) return false;
	disk_sector_count = block.total_sectors;
	if (!load_directory()) return false;
	disk_mounted = true;
	for (size_t index = 0; index < DISK_FILE_COUNT; ++index) {
		if (disk_files[index].name[0] != '\0') cache_name(disk_files[index].name);
	}
	return true;
}

bool is_mounted() {
	return disk_mounted;
}

size_t mounted_device() {
	return active_device;
}

uint64_t free_bytes() {
	if (!disk_mounted) return 0;
	uint64_t used_sectors = 0;
	for (size_t index = 0; index < DISK_FILE_COUNT; ++index) {
		used_sectors += disk_files[index].sector_count;
	}
	const uint64_t data_sectors = disk_sector_count - DATA_LBA;
	return used_sectors >= data_sectors ? 0 : (data_sectors - used_sectors) * SECTOR_SIZE;
}

size_t file_count() {
	size_t count = 0;
	if (disk_mounted) {
		for (const DiskFile& file : disk_files) {
			if (file.name[0] != '\0') ++count;
		}
		return count;
	}
	for (const File& file : files) {
		if (file.used) ++count;
	}
	return count;
}

const char* file_name(size_t index) {
	if (disk_mounted) {
		for (const DiskFile& file : disk_files) {
			if (file.name[0] == '\0') continue;
			if (index == 0) return file.name;
			--index;
		}
		return nullptr;
	}
	for (const File& file : files) {
		if (!file.used) continue;
		if (index == 0) return file.name;
		--index;
	}
	return nullptr;
}

bool create_file(const char* name, const char* content) {
	if (!valid_name(name) || content == nullptr) return false;
	const size_t length = string_length(content);
	if (disk_mounted && !write_file(name, reinterpret_cast<const uint8_t*>(content), length)) {
		return false;
	}
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (files[index].used && equal_name(files[index].name, name)) {
			copy_string(files[index].content, sizeof(files[index].content), content);
			return true;
		}
	}
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (!files[index].used) {
			files[index].used = true;
			copy_string(files[index].name, sizeof(files[index].name), name);
			copy_string(files[index].content, sizeof(files[index].content), content);
			return true;
		}
	}
	return false;
}

bool write_file(const char* name, const uint8_t* data, size_t size) {
	if (!disk_mounted || !valid_name(name) || (data == nullptr && size != 0) ||
		size > 0xFFFFFFFFu) return false;
	const uint32_t required = static_cast<uint32_t>((size + SECTOR_SIZE - 1) / SECTOR_SIZE);
	const int existing = find_disk_file(name);
	int slot = existing;
	if (slot < 0) {
		for (size_t index = 0; index < DISK_FILE_COUNT; ++index) {
			if (disk_files[index].name[0] == '\0') {
				slot = static_cast<int>(index);
				break;
			}
		}
	}
	if (slot < 0) return false;

	uint32_t start_lba = DATA_LBA;
	if (existing >= 0 && required <= disk_files[existing].sector_count) {
		start_lba = disk_files[existing].start_lba;
	} else if (required != 0) {
		start_lba = find_free_extent(required, existing);
		if (start_lba == 0) return false;
	}

	size_t copied = 0;
	for (uint32_t sector = 0; sector < required; ++sector) {
		clear_bytes(sector_buffer, sizeof(sector_buffer));
		const size_t chunk = size - copied < SECTOR_SIZE ? size - copied : SECTOR_SIZE;
		for (size_t byte = 0; byte < chunk; ++byte) sector_buffer[byte] = data[copied + byte];
		if (!Drivers::ATA::write_sectors(active_device, start_lba + sector, 1, sector_buffer)) {
			return false;
		}
		copied += chunk;
	}

	DiskFile updated;
	clear_bytes(&updated, sizeof(updated));
	copy_string(updated.name, sizeof(updated.name), name);
	updated.start_lba = start_lba;
	updated.size_bytes = static_cast<uint32_t>(size);
	updated.sector_count = required;
	disk_files[slot] = updated;
	if (!save_directory()) return false;
	cache_name(name);
	return true;
}

const char* read_file(const char* name) {
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (files[index].used && equal_name(files[index].name, name) &&
			files[index].content[0] != '\0') {
			return files[index].content;
		}
	}
	size_t bytes_read = 0;
	if (read_file(name, reinterpret_cast<uint8_t*>(compatibility_buffer),
		sizeof(compatibility_buffer) - 1, bytes_read)) {
		compatibility_buffer[bytes_read] = '\0';
		return compatibility_buffer;
	}
	return nullptr;
}

bool read_file(const char* name, uint8_t* buffer, size_t capacity, size_t& bytes_read) {
	bytes_read = 0;
	if (!disk_mounted || !valid_name(name) || buffer == nullptr) return false;
	const int index = find_disk_file(name);
	if (index < 0 || disk_files[index].size_bytes > capacity) return false;
	const DiskFile& file = disk_files[index];
	size_t copied = 0;
	for (uint32_t sector = 0; sector < file.sector_count; ++sector) {
		if (!Drivers::ATA::read_sectors(active_device, file.start_lba + sector,
			1, sector_buffer)) return false;
		const size_t chunk = file.size_bytes - copied < SECTOR_SIZE
			? file.size_bytes - copied : SECTOR_SIZE;
		for (size_t byte = 0; byte < chunk; ++byte) buffer[copied + byte] = sector_buffer[byte];
		copied += chunk;
	}
	bytes_read = copied;
	return true;
}

void list_files() {
	if (disk_mounted) {
		for (size_t index = 0; index < DISK_FILE_COUNT; ++index) {
			if (disk_files[index].name[0] != '\0') VGA::println(disk_files[index].name);
		}
		return;
	}
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (files[index].used) VGA::println(files[index].name);
	}
}

} // namespace FS
