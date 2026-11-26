; Tiny ring-3 test programs for the boot self-test (make selftest).
; Position independent: the kernel copies each one to the start of the user
; region and jumps to its first byte. They use Linux x86-64 syscall numbers:
; 1 write, 2 open, 3 close, 39 getpid, 60 exit.
bits 64
default rel

global user_file_start, user_file_end
global user_badptr_start, user_badptr_end
global user_pagefault_start, user_pagefault_end
global user_privileged_start, user_privileged_end

section .rodata

; ---- opens /user.txt, writes a message, closes it, checks that the syscall
; ---- leaves registers alone and that getpid() is 9000; exits with the number
; ---- of bytes written (30), or 255/254 on failure.
MSG_LEN equ 30

user_file_start:
    mov eax, 2                      ; open("/user.txt", O_WRONLY|O_CREAT, 0644)
    lea rdi, [path]
    mov esi, 0x41
    mov edx, 0x1A4
    syscall
    test rax, rax
    js .fail
    mov r12, rax
    mov r8,  0x1111111111111111
    mov r9,  0x2222222222222222
    mov r10, 0x3333333333333333
    mov rbx, 0x4444444444444444
    mov eax, 1                      ; write(fd, msg, 30)
    mov rdi, r12
    lea rsi, [msg]
    mov edx, MSG_LEN
    syscall
    mov r13, rax
    mov eax, 3                      ; close(fd)
    mov rdi, r12
    syscall
    mov eax, 39                     ; getpid()
    syscall
    cmp eax, 9000
    jne .fail
    mov rcx, 0x1111111111111111
    cmp r8, rcx
    jne .regs
    mov rcx, 0x2222222222222222
    cmp r9, rcx
    jne .regs
    mov rcx, 0x3333333333333333
    cmp r10, rcx
    jne .regs
    mov rcx, 0x4444444444444444
    cmp rbx, rcx
    jne .regs
    mov rdi, r13
    mov eax, 60                     ; exit(bytes written)
    syscall
.fail:
    mov edi, 255
    mov eax, 60
    syscall
.regs:
    mov edi, 254
    mov eax, 60
    syscall
path: db "/user.txt", 0
msg:  db "Hello from ring 3 via SYSCALL", 10
user_file_end:

; ---- passes kernel pointers (and one just below its own region) to write()
; ---- and open(): both must fail with -EFAULT (-14). Exits 14 if so, else 1.
user_badptr_start:
    mov eax, 1                      ; write(1, kernel address, 4)
    mov edi, 1
    mov rsi, 0xFFFFFFFF80100000
    mov edx, 4
    syscall
    cmp rax, -14
    jne .fail
    mov eax, 1                      ; write(1, one byte below this region, 4)
    mov edi, 1
    lea rsi, [user_badptr_start]
    dec rsi
    mov edx, 4
    syscall
    cmp rax, -14
    jne .fail
    mov eax, 2                      ; open(kernel address, O_RDONLY)
    mov rdi, 0xFFFFFFFF80100000
    xor esi, esi
    xor edx, edx
    syscall
    cmp rax, -14
    jne .fail
    mov edi, 14
    mov eax, 60
    syscall
.fail:
    mov edi, 1
    mov eax, 60
    syscall
user_badptr_end:

; ---- reads kernel memory: must raise a page fault (vector 14).
user_pagefault_start:
    mov rax, 0xFFFFFFFF80100000
    mov rax, [rax]
    mov edi, 0                      ; never reached
    mov eax, 60
    syscall
user_pagefault_end:

; ---- executes a privileged instruction: must raise #GP (vector 13).
user_privileged_start:
    hlt
    mov edi, 0                      ; never reached
    mov eax, 60
    syscall
user_privileged_end:
