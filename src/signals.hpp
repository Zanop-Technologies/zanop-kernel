#ifndef PROCESS_SIGNALS_HPP
#define PROCESS_SIGNALS_HPP
#include "types.hpp"
namespace Signals {
constexpr int INTERRUPT = 2;
constexpr int TERMINATE = 15;
constexpr int KILL = 9;
using Handler = void (*)(int);
void init();
bool set_handler(int signal, Handler handler);
bool raise(uint32_t pid, int signal);
void dispatch();
bool pending(int signal);
}
#endif
