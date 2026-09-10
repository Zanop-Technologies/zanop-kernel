#ifndef FS_HPP
#define FS_HPP

#include <cstdint>
#include <cstddef>

namespace FS {

struct File {
    char name[32];
    char content[256];
    bool used;
};

void init();
bool create_file(const char* name, const char* content);
const char* read_file(const char* name);
void list_files();

} // namespace FS

#endif