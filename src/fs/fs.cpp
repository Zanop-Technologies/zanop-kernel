#include "fs.hpp"
#include "../drivers/vga.hpp"
#include "../drivers/ata.hpp"

namespace {

constexpr size_t MAX_FILES = 64;
constexpr size_t DISK_FILE_COUNT = 64;
constexpr uint32_t DIRECTORY_LBA = 1;
constexpr uint32_t DIRECTORY_SECTORS = 8;
constexpr uint32_t DATA_LBA = DIRECTORY_LBA + DIRECTORY_SECTORS;
constexpr size_t SECTOR_SIZE = 512;
constexpr size_t NAME_SIZE = 48;
constexpr uint8_t ENTRY_DIRECTORY = 1;
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

bool is_directory_entry(const DiskFile& file) {
	return file.reserved[0] == ENTRY_DIRECTORY;
}

bool canonicalize_path(const char* path, char* output, bool allow_root) {
	if (path == nullptr || path[0] == '\0') return false;
	size_t input = path[0] == '/' ? 1 : 0;
	size_t written = 1;
	size_t segment_start = 1;
	output[0] = '/';
	if (path[input] == '\0') {
		if (!allow_root) return false;
		output[1] = '\0';
		return true;
	}

	while (path[input] != '\0') {
		const char character = path[input];
		if (character == '\\') return false;
		if (character == '/') {
			const size_t segment_size = written - segment_start;
			if (segment_size == 0 ||
				(segment_size == 1 && output[segment_start] == '.') ||
				(segment_size == 2 && output[segment_start] == '.' &&
				 output[segment_start + 1] == '.')) return false;
			++input;
			if (path[input] == '\0') break;
			if (written + 1 >= NAME_SIZE) return false;
			output[written++] = '/';
			segment_start = written;
			continue;
		}
		if (written + 1 >= NAME_SIZE) return false;
		output[written++] = character;
		++input;
	}

	const size_t segment_size = written - segment_start;
	if (segment_size == 0 ||
		(segment_size == 1 && output[segment_start] == '.') ||
		(segment_size == 2 && output[segment_start] == '.' &&
		 output[segment_start + 1] == '.')) return false;
	output[written] = '\0';
	return true;
}

void parent_path(const char* path, char* parent) {
	size_t length = string_length(path);
	while (length > 1 && path[length - 1] != '/') --length;
	if (length <= 1) {
		parent[0] = '/';
		parent[1] = '\0';
		return;
	}
	if (path[length - 1] == '/') --length;
	for (size_t index = 0; index < length; ++index) parent[index] = path[index];
	parent[length] = '\0';
}

bool is_direct_child(const char* parent, const char* candidate, const char** name) {
	const char* tail = nullptr;
	if (equal_name(parent, "/")) {
		if (candidate[0] != '/') return false;
		tail = candidate + 1;
	} else {
		const size_t length = string_length(parent);
		for (size_t index = 0; index < length; ++index) {
			if (candidate[index] != parent[index]) return false;
		}
		if (candidate[length] != '/') return false;
		tail = candidate + length + 1;
	}
	if (*tail == '\0') return false;
	for (const char* character = tail; *character != '\0'; ++character) {
		if (*character == '/') return false;
	}
	if (name != nullptr) *name = tail;
	return true;
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
		DiskFile& file = disk_files[index];
		if (file.name[0] == '\0') continue;
		bool terminated = false;
		for (size_t character = 0; character < NAME_SIZE; ++character) {
			if (file.name[character] == '\0') {
				terminated = true;
				break;
			}
		}
		if (!terminated) return false;
		char canonical_name[NAME_SIZE];
		if (!canonicalize_path(file.name, canonical_name, false)) return false;
		if (!equal_name(file.name, canonical_name)) copy_string(file.name, NAME_SIZE, canonical_name);
		if (file.reserved[0] > ENTRY_DIRECTORY) return false;
		if (is_directory_entry(file)) {
			if (file.start_lba != 0 || file.size_bytes != 0 || file.sector_count != 0) return false;
		} else if (file.start_lba < DATA_LBA || file.start_lba > disk_sector_count ||
			file.sector_count > disk_sector_count - file.start_lba ||
			static_cast<uint64_t>(file.size_bytes) >
				static_cast<uint64_t>(file.sector_count) * SECTOR_SIZE) return false;

		for (size_t other_index = 0; other_index < index; ++other_index) {
			const DiskFile& other = disk_files[other_index];
			if (other.name[0] == '\0') continue;
			if (equal_name(other.name, file.name)) return false;
			if (is_directory_entry(file) || is_directory_entry(other) ||
				file.sector_count == 0 || other.sector_count == 0) continue;
			const uint32_t file_end = file.start_lba + file.sector_count;
			const uint32_t other_end = other.start_lba + other.sector_count;
			if (file.start_lba < other_end && other.start_lba < file_end) return false;
		}
	}
	for (const DiskFile& file : disk_files) {
		if (file.name[0] == '\0') continue;
		char parent[NAME_SIZE];
		parent_path(file.name, parent);
		if (!equal_name(parent, "/")) {
			const int parent_index = find_disk_file(parent);
			if (parent_index < 0 || !is_directory_entry(disk_files[parent_index])) return false;
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
		if (files[index].used && !files[index].is_directory &&
			equal_name(files[index].name, name)) return;
	}
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (!files[index].used || files[index].is_directory) {
			files[index].used = true;
			files[index].is_directory = false;
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

bool is_system_path(const char* path) {
	if (equal_name(path, "/system")) return true;
	static const char prefix[] = "/system/";
	for (size_t index = 0; index < sizeof(prefix) - 1; ++index) {
		if (path[index] != prefix[index]) return false;
	}
	return true;
}

bool seed_system_tree() {
	static const char* const directories[] = {
		"/system", "/system/commands", "/system/programs", "/system/icons",
		"/system/fonts", "/system/engine", "/temp", "/mnt", "/external",
		"/home", "/home/Desktop", "/home/Documents", "/home/Videos", "/home/Music"
	};
	for (const char* directory : directories) {
		if (!FS::create_directory(directory)) return false;
	}
	return true;
}

} // namespace

namespace FS {

void init() {
	clear_bytes(files, sizeof(files));
	disk_mounted = false;
	for (size_t index = 0; index < Drivers::ATA::device_count(); ++index) {
		if (mount(index)) break;
	}
	seed_system_tree();
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
	clear_bytes(files, sizeof(files));
	return seed_system_tree();
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
		if (disk_files[index].name[0] != '\0' && !is_directory_entry(disk_files[index])) {
			cache_name(disk_files[index].name);
		}
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
			if (file.name[0] != '\0' && !is_directory_entry(file)) ++count;
		}
		return count;
	}
	for (const File& file : files) {
		if (file.used && !file.is_directory) ++count;
	}
	return count;
}

const char* file_name(size_t index) {
	if (disk_mounted) {
		for (const DiskFile& file : disk_files) {
			if (file.name[0] == '\0' || is_directory_entry(file)) continue;
			if (index == 0) return file.name;
			--index;
		}
		return nullptr;
	}
	for (const File& file : files) {
		if (!file.used || file.is_directory) continue;
		if (index == 0) return file.name;
		--index;
	}
	return nullptr;
}

bool is_directory(const char* path) {
	char canonical[NAME_SIZE];
	if (!canonicalize_path(path, canonical, true)) return false;
	if (equal_name(canonical, "/")) return true;
	if (disk_mounted) {
		const int index = find_disk_file(canonical);
		return index >= 0 && is_directory_entry(disk_files[index]);
	}
	for (const File& file : files) {
		if (file.used && file.is_directory && equal_name(file.name, canonical)) return true;
	}
	return false;
}

bool create_directory(const char* path) {
	char canonical[NAME_SIZE];
	if (!canonicalize_path(path, canonical, true)) return false;
	if (equal_name(canonical, "/")) return true;
	char parent[NAME_SIZE];
	parent_path(canonical, parent);
	if (!is_directory(parent)) return false;

	if (disk_mounted) {
		const int existing = find_disk_file(canonical);
		if (existing >= 0) return is_directory_entry(disk_files[existing]);
		for (size_t index = 0; index < DISK_FILE_COUNT; ++index) {
			if (disk_files[index].name[0] != '\0') continue;
			DiskFile directory;
			clear_bytes(&directory, sizeof(directory));
			copy_string(directory.name, sizeof(directory.name), canonical);
			directory.reserved[0] = ENTRY_DIRECTORY;
			disk_files[index] = directory;
			if (save_directory()) return true;
			clear_bytes(&disk_files[index], sizeof(disk_files[index]));
			return false;
		}
		return false;
	}

	for (File& file : files) {
		if (file.used && equal_name(file.name, canonical)) return file.is_directory;
	}
	for (File& file : files) {
		if (file.used) continue;
		clear_bytes(&file, sizeof(file));
		file.used = true;
		file.is_directory = true;
		copy_string(file.name, sizeof(file.name), canonical);
		return true;
	}
	return false;
}

size_t directory_entry_count(const char* path) {
	char canonical[NAME_SIZE];
	if (!canonicalize_path(path, canonical, true) || !is_directory(canonical)) return 0;
	size_t count = 0;
	if (disk_mounted) {
		for (const DiskFile& file : disk_files) {
			if (file.name[0] != '\0' && is_direct_child(canonical, file.name, nullptr)) ++count;
		}
	} else {
		for (const File& file : files) {
			if (file.used && is_direct_child(canonical, file.name, nullptr)) ++count;
		}
	}
	return count;
}

bool directory_entry(const char* path, size_t requested_index, DirectoryEntry& entry) {
	char canonical[NAME_SIZE];
	if (!canonicalize_path(path, canonical, true) || !is_directory(canonical)) return false;
	if (disk_mounted) {
		for (const DiskFile& file : disk_files) {
			const char* child_name = nullptr;
			if (file.name[0] == '\0' || !is_direct_child(canonical, file.name, &child_name)) continue;
			if (requested_index-- != 0) continue;
			copy_string(entry.name, sizeof(entry.name), child_name);
			entry.is_directory = is_directory_entry(file);
			return true;
		}
	} else {
		for (const File& file : files) {
			const char* child_name = nullptr;
			if (!file.used || !is_direct_child(canonical, file.name, &child_name)) continue;
			if (requested_index-- != 0) continue;
			copy_string(entry.name, sizeof(entry.name), child_name);
			entry.is_directory = file.is_directory;
			return true;
		}
	}
	return false;
}

bool remove_file(const char* path) {
	char canonical[NAME_SIZE];
	if (!canonicalize_path(path, canonical, false) || is_system_path(canonical)) return false;
	if (disk_mounted) {
		const int index = find_disk_file(canonical);
		if (index < 0 || is_directory_entry(disk_files[index])) return false;
		DiskFile previous = disk_files[index];
		clear_bytes(&disk_files[index], sizeof(disk_files[index]));
		if (save_directory()) {
			for (File& cached : files) {
				if (cached.used && !cached.is_directory && equal_name(cached.name, canonical)) {
					clear_bytes(&cached, sizeof(cached));
				}
			}
			return true;
		}
		disk_files[index] = previous;
		return false;
	}
	for (File& file : files) {
		if (file.used && !file.is_directory && equal_name(file.name, canonical)) {
			clear_bytes(&file, sizeof(file));
			return true;
		}
	}
	return false;
}

bool remove_directory(const char* path) {
	char canonical[NAME_SIZE];
	if (!canonicalize_path(path, canonical, false) || is_system_path(canonical)) return false;
	if (!is_directory(canonical) || directory_entry_count(canonical) != 0) return false;
	if (disk_mounted) {
		const int index = find_disk_file(canonical);
		if (index < 0 || !is_directory_entry(disk_files[index])) return false;
		DiskFile previous = disk_files[index];
		clear_bytes(&disk_files[index], sizeof(disk_files[index]));
		if (save_directory()) return true;
		disk_files[index] = previous;
		return false;
	}
	for (File& file : files) {
		if (file.used && file.is_directory && equal_name(file.name, canonical)) {
			clear_bytes(&file, sizeof(file));
			return true;
		}
	}
	return false;
}

bool create_file(const char* name, const char* content) {
	char canonical[NAME_SIZE];
	if (!canonicalize_path(name, canonical, false) || content == nullptr) return false;
	char parent[NAME_SIZE];
	parent_path(canonical, parent);
	if (!is_directory(parent) || is_directory(canonical)) return false;
	const size_t length = string_length(content);
	if (disk_mounted && !write_file(canonical, reinterpret_cast<const uint8_t*>(content), length)) {
		return false;
	}
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (files[index].used && !files[index].is_directory &&
			equal_name(files[index].name, canonical)) {
			copy_string(files[index].content, sizeof(files[index].content), content);
			return true;
		}
	}
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (!files[index].used || files[index].is_directory) {
			clear_bytes(&files[index], sizeof(files[index]));
			files[index].used = true;
			copy_string(files[index].name, sizeof(files[index].name), canonical);
			copy_string(files[index].content, sizeof(files[index].content), content);
			return true;
		}
	}
	return false;
}

bool write_file(const char* name, const uint8_t* data, size_t size) {
	char canonical[NAME_SIZE];
	if (!disk_mounted || !canonicalize_path(name, canonical, false) ||
		(data == nullptr && size != 0) ||
		size > 0xFFFFFFFFu) return false;
	char parent[NAME_SIZE];
	parent_path(canonical, parent);
	if (!is_directory(parent)) return false;
	const uint32_t required = static_cast<uint32_t>((size + SECTOR_SIZE - 1) / SECTOR_SIZE);
	const int existing = find_disk_file(canonical);
	if (existing >= 0 && is_directory_entry(disk_files[existing])) return false;
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
	copy_string(updated.name, sizeof(updated.name), canonical);
	updated.start_lba = start_lba;
	updated.size_bytes = static_cast<uint32_t>(size);
	updated.sector_count = required;
	disk_files[slot] = updated;
	if (!save_directory()) return false;
	cache_name(canonical);
	return true;
}

const char* read_file(const char* name) {
	char canonical[NAME_SIZE];
	if (!canonicalize_path(name, canonical, false)) return nullptr;
	for (size_t index = 0; index < MAX_FILES; ++index) {
		if (files[index].used && !files[index].is_directory &&
			equal_name(files[index].name, canonical) &&
			files[index].content[0] != '\0') {
			return files[index].content;
		}
	}
	size_t bytes_read = 0;
	if (read_file(canonical, reinterpret_cast<uint8_t*>(compatibility_buffer),
		sizeof(compatibility_buffer) - 1, bytes_read)) {
		compatibility_buffer[bytes_read] = '\0';
		return compatibility_buffer;
	}
	return nullptr;
}

bool read_file(const char* name, uint8_t* buffer, size_t capacity, size_t& bytes_read) {
	bytes_read = 0;
	char canonical[NAME_SIZE];
	if (!disk_mounted || !canonicalize_path(name, canonical, false) || buffer == nullptr) return false;
	const int index = find_disk_file(canonical);
	if (index < 0 || disk_files[index].size_bytes > capacity) return false;
	const DiskFile& file = disk_files[index];
	if (is_directory_entry(file)) return false;
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

bool file_size(const char* name, size_t& size) {
	size = 0;
	char canonical[NAME_SIZE];
	if (!canonicalize_path(name, canonical, false)) return false;
	if (disk_mounted) {
		const int index = find_disk_file(canonical);
		if (index < 0 || is_directory_entry(disk_files[index])) return false;
		size = disk_files[index].size_bytes;
		return true;
	}
	for (const File& file : files) {
		if (file.used && !file.is_directory && equal_name(file.name, canonical)) {
			size = string_length(file.content);
			return true;
		}
	}
	return false;
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
