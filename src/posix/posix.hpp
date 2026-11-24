// POSIX layer: public interface.
//
// The layer is split in two so the logic can be unit-tested on a host:
//   * posix.cpp          - fd tables, path handling, syscall semantics.
//                          Depends only on the Backend interface below.
//   * kernel_backend.cpp - binds Backend to the Zanop kernel (FS, Terminal,
//                          Process, Memory, Signals).
#pragma once
#include "posix_types.hpp"

namespace posix {

// Result of Backend::stat. Paths handed to a Backend are always absolute
// and canonical (no ".", "..", doubled or trailing slashes).
struct FsStat {
    bool is_dir;
    u64 size;
};

struct DirEntry {
    char name[PATH_LIMIT];
    bool is_dir;
};

// Everything the portable core needs from the kernel. Functions returning
// i64 return a non-negative result or -errno. Optional entries may be null.
struct Backend {
    // Filesystem.
    i64 (*fs_stat)(const char* path, FsStat* out);
    i64 (*fs_read)(const char* path, u64 offset, void* buffer, usize count);
    i64 (*fs_write)(const char* path, u64 offset, const void* buffer, usize count);
    i64 (*fs_truncate)(const char* path, u64 size);
    i64 (*fs_create)(const char* path);
    i64 (*fs_unlink)(const char* path);
    i64 (*fs_mkdir)(const char* path);
    i64 (*fs_rmdir)(const char* path);
    i64 (*fs_rename)(const char* from, const char* to);       // optional
    i64 (*fs_readdir)(const char* path, usize index, DirEntry* out); // 1, 0 at end, or -errno

    // Console (fds 0, 1, 2).
    i64 (*con_read)(void* buffer, usize count);
    i64 (*con_write)(const void* buffer, usize count);

    // Process control.
    u32 (*cur_pid)();
    bool (*pid_alive)(u32 pid);
    i64 (*proc_exit)(u32 pid, int status);
    i64 (*proc_kill)(u32 pid, int signal);
    i64 (*proc_yield)();

    // Memory (backs anonymous mmap).
    void* (*mem_alloc)(usize size, usize align);
    void (*mem_free)(void* ptr);

    // Time. Optional: the kernel has no timer yet.
    i64 (*clock_gettime)(int clock_id, timespec* out);
};

// Install the backend and reset all state.
void init(const Backend* backend);

// Dispatch one syscall using the Linux x86-64 numbering in posix_types.hpp.
// Returns the result, or -errno on failure.
i64 syscall(u64 number, u64 a1 = 0, u64 a2 = 0, u64 a3 = 0, u64 a4 = 0,
            u64 a5 = 0, u64 a6 = 0);

// Called when a process is known to be gone: closes its descriptors.
void reap(u32 pid);

// Ring-3 support. Between enter_user_context() and leave_user_context(),
// syscall() serves a user program: pointer arguments must lie inside
// [lo, hi), descriptors are tracked under `pid`, and exit() only drops that
// state (the caller of the program decides what happens next).
void enter_user_context(u32 pid, u64 lo, u64 hi);
void leave_user_context();   // also closes the program's descriptors
bool in_user_context();

// Binds the layer to the running kernel (kernel_backend.cpp).
void init_kernel();

} // namespace posix
