// Minimal C library memory/string primitives for the freestanding kernel.
//
// The compiler emits calls to memcpy/memmove/memset/memcmp on its own (struct
// copies, zero-initialisation), and -nostdlib provides none of them. They are
// weak so a fuller libc implementation can override them later.
//
// Each body is inline asm on purpose: a plain C loop can be turned back into
// a call to the very function being defined, which recurses forever.
#include "posix_types.hpp"

extern "C" {

__attribute__((weak)) void* memcpy(void* dest, const void* src, posix::usize n) {
    void* ret = dest;
    __asm__ volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

__attribute__((weak)) void* memmove(void* dest, const void* src, posix::usize n) {
    void* ret = dest;
    if (dest == src || n == 0) return ret;
    if (dest < src) {
        __asm__ volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
    } else {
        // Overlapping with dest above src: copy backwards.
        dest = static_cast<posix::u8*>(dest) + n - 1;
        src = static_cast<const posix::u8*>(src) + n - 1;
        __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
    }
    return ret;
}

__attribute__((weak)) void* memset(void* dest, int value, posix::usize n) {
    void* ret = dest;
    __asm__ volatile("rep stosb" : "+D"(dest), "+c"(n) : "a"(value) : "memory");
    return ret;
}

__attribute__((weak, no_builtin)) int memcmp(const void* a, const void* b, posix::usize n) {
    const posix::u8* x = static_cast<const posix::u8*>(a);
    const posix::u8* y = static_cast<const posix::u8*>(b);
    for (posix::usize i = 0; i < n; ++i)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

__attribute__((weak, no_builtin)) posix::usize strlen(const char* s) {
    posix::usize n = 0;
    while (s[n]) ++n;
    return n;
}

} // extern "C"
