; Ring-3 support: SYSCALL entry, entering/leaving user mode, CPU exception stubs.
;
; Segment layout (see usermode.cpp):
;   0x08 kernel code   0x10 kernel data   0x18 user data   0x20 user code
;   STAR[47:32] = 0x08 -> SYSCALL loads CS=0x08, SS=0x10
;   STAR[63:48] = 0x10 -> SYSRET  loads CS=0x20|3, SS=0x18|3
bits 64
default rel

global zanop_syscall_entry
global zanop_enter_user
global zanop_user_return
global zanop_exception_stubs

extern zanop_syscall_stack_top
extern zanop_syscall_dispatch
extern zanop_exception_dispatch

section .bss
align 8
user_rsp_save:      resq 1
saved_kernel_rsp:   resq 1

section .text

; ---------------------------------------------------------------------------
; SYSCALL entry. Linux x86-64 convention: rax = number, args in
; rdi, rsi, rdx, r10, r8, r9. rcx = user rip and r11 = user rflags (set by the
; CPU). Everything except rax, rcx and r11 is preserved for the caller, as
; musl expects. Interrupts are masked on entry (SFMASK) and stay masked.
; ---------------------------------------------------------------------------
zanop_syscall_entry:
    mov [user_rsp_save], rsp
    mov rsp, [zanop_syscall_stack_top]
    push qword [user_rsp_save]
    push rcx
    push r11
    push rax
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    mov rdi, rsp                ; SyscallFrame*
    cld
    call zanop_syscall_dispatch
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    add rsp, 8                  ; drop saved rax: the result is in rax
    pop r11
    pop rcx
    pop rsp                     ; back on the user stack
    o64 sysret

; ---------------------------------------------------------------------------
; long long zanop_enter_user(uint64_t entry, uint64_t user_rsp)
; Runs a user program until it exits or faults, then returns its status to the
; caller of this function. Kernel callee-saved registers and flags are kept on
; the kernel stack and restored by zanop_user_return.
; ---------------------------------------------------------------------------
zanop_enter_user:
    pushfq
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [saved_kernel_rsp], rsp
    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    push qword 0x1B             ; ss
    push rsi                    ; rsp
    push qword 0x202            ; rflags: IF set, IOPL 0
    push qword 0x23             ; cs
    push rdi                    ; rip
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

; void zanop_user_return(long long status)   [does not return to its caller]
; Abandons whatever kernel stack is in use and resumes after zanop_enter_user.
zanop_user_return:
    mov rax, rdi
    mov rsp, [saved_kernel_rsp]
    mov cx, 0x10
    mov ds, cx
    mov es, cx
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    popfq
    ret

; ---------------------------------------------------------------------------
; CPU exceptions 0..31. The CPU pushes an error code for some vectors; for the
; others a zero is pushed so every frame has the same shape.
; ---------------------------------------------------------------------------
%macro EXC_NOERR 1
exc_stub_%1:
    push qword 0
    push qword %1
    jmp exception_common
%endmacro

%macro EXC_ERR 1
exc_stub_%1:
    push qword %1
    jmp exception_common
%endmacro

EXC_NOERR 0
EXC_NOERR 1
EXC_NOERR 2
EXC_NOERR 3
EXC_NOERR 4
EXC_NOERR 5
EXC_NOERR 6
EXC_NOERR 7
EXC_ERR   8
EXC_NOERR 9
EXC_ERR   10
EXC_ERR   11
EXC_ERR   12
EXC_ERR   13
EXC_ERR   14
EXC_NOERR 15
EXC_NOERR 16
EXC_ERR   17
EXC_NOERR 18
EXC_NOERR 19
EXC_NOERR 20
EXC_ERR   21
EXC_NOERR 22
EXC_NOERR 23
EXC_NOERR 24
EXC_NOERR 25
EXC_NOERR 26
EXC_NOERR 27
EXC_NOERR 28
EXC_ERR   29
EXC_ERR   30
EXC_NOERR 31

exception_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp                ; ExceptionFrame*
    cld
    call zanop_exception_dispatch
.hang:
    cli
    hlt
    jmp .hang

section .rodata
align 8
zanop_exception_stubs:
%assign i 0
%rep 32
    dq exc_stub_ %+ i
%assign i i+1
%endrep
