#include "signals.hpp"
#include "process.hpp"
namespace {
Signals::Handler handlers[32] = {};
volatile uint32_t pending_mask = 0;
}
namespace Signals {
void init() { for (size_t i = 0; i < 32; ++i) handlers[i] = nullptr; pending_mask = 0; }
bool set_handler(int signal, Handler handler) {
    if (signal <= 0 || signal >= 32 || signal == TERMINATE) return false;
    handlers[signal] = handler;
    return true;
}
bool raise(uint32_t pid, int signal) {
    if (signal <= 0 || signal >= 32) return false;
    const uint32_t target = pid ? pid : Process::current_pid();
    if (pid != 0 && target != Process::current_pid()) return false;
    if (signal == KILL || signal == TERMINATE) {
        return Process::terminate(target, 128 + signal);
    }
    pending_mask |= static_cast<uint32_t>(1u << signal);
    return true;
}
void dispatch() {
    const uint32_t queued = pending_mask;
    pending_mask = 0;
    for (int signal = 1; signal < 32; ++signal) {
        if ((queued & (1u << signal)) == 0) continue;
        if (handlers[signal]) handlers[signal](signal);
    }
}
bool pending(int signal) { return signal > 0 && signal < 32 && (pending_mask & (1u << signal)) != 0; }
}
