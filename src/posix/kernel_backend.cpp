// Binds the portable POSIX core (posix.cpp) to the running Zanop kernel.
#include "posix.hpp"
#include "../fs/fs.hpp"
#include "../memory/allocator.hpp"
#include "../process.hpp"
#include "../signals.hpp"
#include "../terminal.hpp"

namespace posix {
namespace {

// The filesystem keeps whole files per entry and has no partial I/O, so
// read/write/truncate go through one temporary buffer sized to the file.
// That makes them O(file size) and bounds files by the 100 KiB kernel heap;
// see docs/posix.md.
constexpr u64 MAX_FILE_BYTES = 0xFFFFFFFFull; // on-disk size field is 32 bits

bool starts_with(const char* s, const char* prefix) {
    for (usize i = 0; prefix[i]; ++i)
        if (s[i] != prefix[i]) return false;
    return true;
}

i64 denied(const char* path) {
    return starts_with(path, "/system/") ? -err::ACCES : -err::IO;
}

u8* alloc_zeroed(usize size) {
    u8* p = static_cast<u8*>(Memory::allocate(size ? size : 1, 1));
    if (!p) return nullptr;
    for (usize i = 0; i < size; ++i) p[i] = 0;
    return p;
}

i64 k_stat(const char* path, FsStat* out) {
    if (FS::is_directory(path)) {
        out->is_dir = true;
        out->size = 0;
        return 0;
    }
    size_t size = 0;
    if (!FS::file_size(path, size)) return -err::NOENT;
    out->is_dir = false;
    out->size = size;
    return 0;
}

i64 k_read(const char* path, u64 offset, void* buffer, usize count) {
    size_t size = 0;
    if (!FS::file_size(path, size)) return -err::NOENT;
    if (offset >= size) return 0;
    const usize avail = static_cast<usize>(size - offset);
    if (count > avail) count = avail;
    u8* out = static_cast<u8*>(buffer);

    if (!FS::is_mounted()) { // RAM-only file (256 byte content)
        const char* content = FS::read_file(path);
        if (!content) return -err::IO;
        for (usize i = 0; i < count; ++i) out[i] = static_cast<u8>(content[offset + i]);
        return static_cast<i64>(count);
    }
    u8* tmp = static_cast<u8*>(Memory::allocate(size, 1));
    if (!tmp) return -err::NOMEM;
    size_t got = 0;
    const bool ok = FS::read_file(path, tmp, size, got);
    if (ok && got >= offset + count)
        for (usize i = 0; i < count; ++i) out[i] = tmp[offset + i];
    Memory::deallocate(tmp);
    return (ok && got >= offset + count) ? static_cast<i64>(count) : -err::IO;
}

// Rewrites a file as `new_size` bytes whose prefix is the old content,
// optionally overlaid with `data` at `offset`.
i64 rewrite(const char* path, usize new_size, u64 offset, const void* data, usize count) {
    if (!FS::is_mounted()) return -err::ROFS;
    size_t old = 0;
    if (!FS::file_size(path, old)) return -err::NOENT;
    const usize cap = new_size > old ? new_size : old;
    if (cap == 0) return FS::write_file(path, nullptr, 0) ? 0 : -err::IO;
    u8* buf = alloc_zeroed(cap);
    if (!buf) return -err::NOMEM;
    size_t got = 0;
    if (old && !FS::read_file(path, buf, old, got)) {
        Memory::deallocate(buf);
        return -err::IO;
    }
    const u8* in = static_cast<const u8*>(data);
    for (usize i = 0; i < count; ++i) buf[offset + i] = in[i];
    const bool ok = FS::write_file(path, buf, new_size);
    Memory::deallocate(buf);
    return ok ? 0 : -err::NOSPC;
}

i64 k_write(const char* path, u64 offset, const void* buffer, usize count) {
    if (!FS::is_mounted()) return -err::ROFS;
    const u64 end = offset + count;
    if (end < offset || end > MAX_FILE_BYTES) return -err::FBIG;
    size_t old = 0;
    if (!FS::file_size(path, old)) return -err::NOENT;
    const usize new_size = end > old ? static_cast<usize>(end) : old;
    const i64 r = rewrite(path, new_size, offset, buffer, count);
    return r < 0 ? r : static_cast<i64>(count);
}

i64 k_truncate(const char* path, u64 size) {
    if (size > MAX_FILE_BYTES) return -err::FBIG;
    return rewrite(path, static_cast<usize>(size), 0, nullptr, 0);
}

i64 k_create(const char* path) { return FS::create_file(path, "") ? 0 : -err::NOSPC; }
i64 k_unlink(const char* path) { return FS::remove_file(path) ? 0 : denied(path); }
i64 k_mkdir(const char* path) { return FS::create_directory(path) ? 0 : -err::NOSPC; }
i64 k_rmdir(const char* path) { return FS::remove_directory(path) ? 0 : denied(path); }

i64 k_rename(const char* from, const char* to) {
    if (!FS::is_mounted()) return -err::ROFS;
    if (starts_with(from, "/system/") || starts_with(to, "/system/")) return -err::ACCES;
    size_t size = 0;
    if (!FS::file_size(from, size)) return -err::NOENT;
    u8* buf = nullptr;
    if (size) {
        buf = static_cast<u8*>(Memory::allocate(size, 1));
        if (!buf) return -err::NOMEM;
        size_t got = 0;
        if (!FS::read_file(from, buf, size, got)) {
            Memory::deallocate(buf);
            return -err::IO;
        }
    }
    const bool wrote = FS::write_file(to, buf, size);
    if (buf) Memory::deallocate(buf);
    if (!wrote) return -err::NOSPC;
    if (!FS::remove_file(from)) {
        FS::remove_file(to); // roll back so the file is not duplicated
        return -err::IO;
    }
    return 0;
}

i64 k_readdir(const char* path, usize index, DirEntry* out) {
    FS::DirectoryEntry e;
    if (!FS::directory_entry(path, index, e)) return 0;
    usize i = 0;
    while (i + 1 < PATH_LIMIT && e.name[i]) {
        out->name[i] = e.name[i];
        ++i;
    }
    out->name[i] = '\0';
    out->is_dir = e.is_directory;
    return 1;
}

i64 k_con_read(void* buffer, usize count) {
    u8* out = static_cast<u8*>(buffer);
    usize n = 0;
    while (n < count) { // line-buffered: returns after a newline
        char c;
        if (!Terminal::try_read(c)) {
            __asm__ volatile("hlt");
            continue;
        }
        out[n++] = static_cast<u8>(c);
        if (c == '\n') break;
    }
    return static_cast<i64>(n);
}

i64 k_con_write(const void* buffer, usize count) {
    Terminal::write_bytes(static_cast<const u8*>(buffer), count);
    return static_cast<i64>(count);
}

u32 k_cur_pid() { return Process::current_pid(); }

bool k_pid_alive(u32 pid) {
    Process::ProcessInfo info;
    for (size_t i = 0; Process::info(i, info); ++i)
        if (info.pid == pid && info.state != Process::State::Exited) return true;
    return false;
}

i64 k_exit(u32 pid, int status) { return Process::terminate(pid, status) ? 0 : -err::PERM; }
i64 k_kill(u32 pid, int signal) { return Signals::raise(pid, signal) ? 0 : -err::INVAL; }
i64 k_yield() { return Process::yield() ? 0 : -err::AGAIN; }
void* k_alloc(usize size, usize align) { return Memory::allocate(size, align); }
void k_free(void* p) { Memory::deallocate(p); }

const Backend kernel_backend = {
    k_stat, k_read, k_write, k_truncate, k_create, k_unlink, k_mkdir, k_rmdir,
    k_rename, k_readdir, k_con_read, k_con_write, k_cur_pid, k_pid_alive,
    k_exit, k_kill, k_yield, k_alloc, k_free,
    nullptr, // clock_gettime: the kernel has no timer or RTC date yet
};

} // namespace

void init_kernel() { init(&kernel_backend); }

} // namespace posix
