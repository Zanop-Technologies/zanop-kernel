#include "syscall.hpp"
#include "terminal.hpp"
#include "fs/fs.hpp"
#include "process.hpp"
#include "signals.hpp"
#include "memory/allocator.hpp"
#include "memory/paging.hpp"
#include "posix/posix.hpp"
namespace {
constexpr size_t HANDLE_COUNT = 16;
struct Handle { bool used; char path[48]; size_t offset; bool writable; } handles[HANDLE_COUNT] = {};
void copy(char* out, const char* in, size_t max) { size_t i=0; if(in) while(i+1<max && in[i]) { out[i]=in[i]; ++i; } out[i]=0; }
size_t text_length(const char* text, size_t max) { size_t n=0; if (!text) return max; while(n<max && text[n]) ++n; return n; }
}
namespace Syscall {
long long posix_invoke(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6) {
    return posix::syscall(number, a1, a2, a3, a4, a5, a6);
}

long long invoke(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3) {
    switch (number) {
    case Write: {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(a2);
        const size_t size = static_cast<size_t>(a3);
        if (size > 65536 || (!bytes && size)) return -1;
        if (a1 == 1 || a1 == 2) {
            if (!bytes && size) return -1;
            Terminal::write_bytes(bytes, size);
            return static_cast<long long>(size);
        }
        if (a1 >= HANDLE_COUNT || !handles[a1].used || !handles[a1].writable || (!bytes && size)) return -1;
        size_t old_size=0;
        for (size_t capacity=256; capacity<=65536; capacity*=2) {
            uint8_t* existing=static_cast<uint8_t*>(Memory::allocate(capacity, alignof(uint8_t)));
            if (!existing) return -1;
            const bool ok=FS::read_file(handles[a1].path,existing,capacity,old_size);
            Memory::deallocate(existing);
            if (ok || old_size==0) break;
        }
        if (handles[a1].offset > old_size || size > 65536 - handles[a1].offset) return -1;
        const size_t new_size = (handles[a1].offset + size > old_size) ? handles[a1].offset + size : old_size;
        if (!new_size) {
            if (!FS::write_file(handles[a1].path, nullptr, 0)) return -1;
            return 0;
        }
        uint8_t* content=static_cast<uint8_t*>(Memory::allocate(new_size, alignof(uint8_t)));
        if (!content) return -1;
        for(size_t i=0;i<new_size;++i) content[i]=0;
        size_t read_size=0;
        if (old_size) {
            if (!FS::read_file(handles[a1].path,content,new_size,read_size)) { Memory::deallocate(content); return -1; }
        }
        for(size_t i=0;i<size;++i) content[handles[a1].offset+i]=bytes[i];
        const bool written=FS::write_file(handles[a1].path,content,new_size);
        Memory::deallocate(content);
        if (!written) return -1;
        handles[a1].offset += size;
        return static_cast<long long>(size);
    }
    case Read: {
        if (a1 == 0) {
            uint8_t* destination = reinterpret_cast<uint8_t*>(a2);
            if (!destination && a3) return -1;
            size_t count = 0;
            while (count < a3) {
                char character;
                if (!Terminal::try_read(character)) { __asm__ volatile("hlt"); continue; }
                destination[count++] = static_cast<uint8_t>(character);
                if (character == '\n') break;
            }
            return static_cast<long long>(count);
        }
        if (a1 >= HANDLE_COUNT || !handles[a1].used || !a2 || a3 == 0 || a3 > 65536) return -1;
        size_t file_size = 0;
        for (size_t capacity=256; capacity<=65536; capacity*=2) {
            uint8_t* buffer = static_cast<uint8_t*>(Memory::allocate(capacity, alignof(uint8_t)));
            if (!buffer) return -1;
            const bool ok = FS::read_file(handles[a1].path, buffer, capacity, file_size);
            if (ok) {
                if (handles[a1].offset >= file_size) { Memory::deallocate(buffer); return 0; }
                size_t amount = file_size - handles[a1].offset;
                if (amount > a3) amount = static_cast<size_t>(a3);
                uint8_t* destination = reinterpret_cast<uint8_t*>(a2);
                for (size_t i=0;i<amount;++i) destination[i]=buffer[handles[a1].offset+i];
                handles[a1].offset += amount;
                Memory::deallocate(buffer);
                return static_cast<long long>(amount);
            }
            Memory::deallocate(buffer);
            if (file_size == 0) break;
        }
        return -1;
    }
    case Open: {
        const char* path = reinterpret_cast<const char*>(a1);
        const uint64_t flags = a2;
        if (!path || text_length(path, sizeof(handles[0].path)) == sizeof(handles[0].path)) return -1;
        char path_copy[sizeof(handles[0].path)];
        copy(path_copy, path, sizeof(path_copy));
        const bool writable = (flags & (WriteOnly | Create | Truncate | Append)) != 0;
        const bool existing_file = FS::read_file(path_copy) != nullptr;
        if ((flags & Create) && !existing_file && !FS::create_file(path_copy, "")) return -1;
        if (!FS::is_directory(path_copy) && !existing_file && !(flags & Create)) return -1;
        if (FS::is_directory(path_copy) && writable) return -1;
        if (flags & Truncate) {
            if (!writable || !FS::write_file(path_copy, nullptr, 0)) return -1;
        }
        for (size_t i=0; i<HANDLE_COUNT; ++i) if (!handles[i].used) {
            handles[i].used=true;
            handles[i].offset=0;
            if (flags & Append) {
                size_t size=0;
                for (size_t capacity=256; capacity<=65536; capacity*=2) {
                    uint8_t* scratch=static_cast<uint8_t*>(Memory::allocate(capacity, alignof(uint8_t)));
                    if (!scratch) break;
                    const bool ok=FS::read_file(path_copy,scratch,capacity,size);
                    Memory::deallocate(scratch);
                    if (ok) break;
                    if (size==0) break;
                }
                handles[i].offset=size;
            }
            handles[i].writable=writable;
            copy(handles[i].path, path_copy, sizeof(handles[i].path));
            return static_cast<long long>(i);
        }
        return -1;
    }
    case Close: if (a1 >= HANDLE_COUNT || !handles[a1].used) return -1; handles[a1].used=false; handles[a1].path[0]=0; handles[a1].offset=0; handles[a1].writable=false; return 0;
    case Yield: return Process::yield() ? 0 : -1;
    case GetPid: return Process::current_pid();
    case MapMemory: return Memory::Paging::map_page(static_cast<uintptr_t>(a1), static_cast<uintptr_t>(a2), a3) ? 0 : -1;
    case UnmapMemory: return Memory::Paging::unmap_page(static_cast<uintptr_t>(a1)) ? 0 : -1;
    case Kill: return Signals::raise(static_cast<uint32_t>(a1), static_cast<int>(a2)) ? 0 : -1;
    case Exit: return Process::terminate(Process::current_pid(), static_cast<int>(a1)) ? 0 : -1;
    default: return -1;
    }
}
}
