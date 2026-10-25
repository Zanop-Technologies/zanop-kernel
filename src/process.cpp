#include "process.hpp"

namespace {
struct Slot {
    Process::ProcessInfo info;
    Process::Entry entry;
    int argc;
    const char* const* argv;
};
Slot slots[Process::MAX_PROCESSES] = {};
uint32_t next_pid = 1;
int active = -1;
int find_pid(uint32_t pid) {
    for (size_t i=0;i<Process::MAX_PROCESSES;++i)
        if (slots[i].info.state != Process::State::Empty && slots[i].info.pid == pid) return static_cast<int>(i);
    return -1;
}

void copy_name(char* out, const char* name) {
    size_t i = 0;
    if (name) while (i < 31 && name[i]) { out[i] = name[i]; ++i; }
    out[i] = '\0';
}
}

namespace Process {
void init() {
    for (size_t i = 0; i < MAX_PROCESSES; ++i) slots[i] = {};
    next_pid = 2;
    slots[0].info.pid = 1;
    slots[0].info.state = State::Running;
    slots[0].info.priority = 1;
    copy_name(slots[0].info.name, "kernel");
    active = 0;
}

int create(const char* name, Entry entry, int argc, const char* const* argv) {
    if (!entry || argc < 0 || (argc > 0 && argv == nullptr)) return -1;
    for (size_t i = 1; i < MAX_PROCESSES; ++i) {
        if (slots[i].info.state != State::Empty && slots[i].info.state != State::Exited) continue;
        slots[i] = {};
        slots[i].info.pid = next_pid++;
        if (next_pid < 2) next_pid = 2;
        slots[i].info.state = State::Ready;
        slots[i].info.priority = 1;
        copy_name(slots[i].info.name, name);
        slots[i].entry = entry;
        slots[i].argc = argc;
        slots[i].argv = argv;
        return static_cast<int>(slots[i].info.pid);
    }
    return -1;
}

bool terminate(uint32_t pid, int status) {
    const int index = find_pid(pid);
    if (index <= 0 || slots[index].info.state == State::Exited) return false;
    slots[index].info.exit_code = status;
    slots[index].info.state = State::Exited;
    return true;
}

bool yield() {
    if (active >= 0 && slots[active].info.state == State::Running && active != 0)
        slots[active].info.state = State::Ready;
    const int previous = active;
    for (size_t step = 1; step <= MAX_PROCESSES; ++step) {
        const size_t candidate = previous < 0 ? step % MAX_PROCESSES : (static_cast<size_t>(previous) + step) % MAX_PROCESSES;
        if (slots[candidate].info.state != State::Ready) continue;
        active = static_cast<int>(candidate);
        slots[candidate].info.state = State::Running;
        const int status = slots[candidate].entry(slots[candidate].argc, slots[candidate].argv);
        if (slots[candidate].info.state == State::Running) {
            slots[candidate].info.exit_code = status;
            slots[candidate].info.state = State::Exited;
        }
        active = 0;
        slots[0].info.state = State::Running;
        return true;
    }
    active = 0;
    slots[0].info.state = State::Running;
    return false;
}

uint32_t current_pid() { return active < 0 ? 0 : slots[active].info.pid; }
void set_quantum(uint32_t pid, uint8_t priority) {
    const int index=find_pid(pid);
    if(index>=0 && priority>0) slots[index].info.priority=priority;
}
size_t count() {
    size_t total = 0;
    for (const Slot& slot : slots) if (slot.info.state != State::Empty && slot.info.state != State::Exited) ++total;
    return total;
}
bool info(size_t index, ProcessInfo& out) {
    for (const Slot& slot : slots) {
        if (slot.info.state == State::Empty) continue;
        if (index-- == 0) { out = slot.info; return true; }
    }
    return false;
}
} // namespace Process
