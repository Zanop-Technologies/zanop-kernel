; =====================================================================
; Zanop Kernel — interrupts.asm
; 64-bit interrupt service routine stubs. The 32-bit pusha/iretd version
; from earlier builds no longer applies now that boot.asm transitions to
; long mode before kernel_main runs — x86_64 has no pusha, and iretq is
; required instead of iretd.
; =====================================================================

extern keyboard_interrupt_handler

%macro PUSH_REGS 0
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
%endmacro

%macro POP_REGS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

bits 64

; ----- IRQ1: keyboard (remapped to vector 33 by the PIC driver) -----
global isr33
isr33:
    PUSH_REGS
    call keyboard_interrupt_handler
    POP_REGS
    iretq