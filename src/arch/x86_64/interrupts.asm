%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0          ; dummy error code
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1
    jmp isr_common
%endmacro

extern isr_handler

isr_common:
    pusha
    call isr_handler
    popa
    add esp, 8
    iretd

; example vectors — expand as needed
ISR_NOERR 0   ; divide by zero
ISR_NOERR 6   ; invalid opcode
ISR_ERR   13  ; general protection fault
ISR_ERR   14  ; page fault