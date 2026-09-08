global _start
extern kernel_main

section .boot
bits 32

_start:
    mov esp, stack_top

    ; TODO: set up paging, enter long mode
    ; TODO: load GDT, jump to 64-bit code segment

    call kernel_main
    hlt

section .bss
align 16
stack_bottom:
    resb 16384 ; 16 KiB stack
stack_top: