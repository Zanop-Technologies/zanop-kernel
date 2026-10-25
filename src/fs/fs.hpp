#ifndef FS_HPP
#define FS_HPP

#include "../types.hpp"

namespace FS {

struct File {
    char name[48];
    char content[256];
    bool used;
    bool is_directory;
};

struct DirectoryEntry {
    char name[48];
    bool is_directory;
};

void init();
bool format(std::size_t device_id);
bool mount(std::size_t device_id);
bool is_mounted();
std::size_t mounted_device();
std::uint64_t free_bytes();
std::size_t file_count();
const char* file_name(std::size_t index);
bool create_directory(const char* path);
bool remove_directory(const char* path);
bool remove_file(const char* path);
bool is_directory(const char* path);
std::size_t directory_entry_count(const char* path);
bool directory_entry(const char* path, std::size_t index, DirectoryEntry& entry);
bool create_file(const char* name, const char* content);
const char* read_file(const char* name);
bool read_file(const char* name, std::uint8_t* buffer, std::size_t capacity,
               std::size_t& bytes_read);
bool write_file(const char* name, const std::uint8_t* data, std::size_t size);
void list_files();

} // namespace FS

#endif