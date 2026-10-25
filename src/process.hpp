#ifndef PROCESS_PROCESS_HPP
#define PROCESS_PROCESS_HPP
#include "../types.hpp"

namespace Process {

constexpr size_t MAX_PROCESSES = 16;
using Entry = int (*)(int argc, const char* const* argv);

enum class State : uint8_t { Empty, Ready, Running, Blocked, Exited };
struct ProcessInfo {
    uint32_t pid;
    State state;
    int exit_code;
    uint8_t priority;
    char name[32];
};

void init();
int create(const char* name, Entry entry, int argc = 0, const char* const* argv = nullptr);
bool terminate(uint32_t pid, int status = 0);
bool yield();
uint32_t current_pid();
void set_quantum(uint32_t pid, uint8_t priority);
size_t count();
bool info(size_t index, ProcessInfo& out);

} // namespace Process
#endif
