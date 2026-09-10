; =====================================================================
; Zanop Kernel (C++ rewrite) — boot.asm
; Multiboot2 header + 32-bit -> 64-bit long-mode transition.
; This is the corrected version from the Rust build (v0.0.4/v0.0.5
; debugging session) — gdt64 lives in the low-memory .boot section
; (not .rodata) since it's used before paging/long-mode is active,
; and page tables/stack live in .bss.boot (also low memory) rather
; than the default higher-half .bss.
; =====================================================================

global _start
extern kernel_main

section .multiboot
align 8
multiboot_header:
    dd 0xe85250d6
    dd 0
    dd multiboot_header_end - multiboot_header
    dd -(0xe85250d6 + 0 + (multiboot_header_end - multiboot_header))
    dw 0
    dw 0
    dd 8
multiboot_header_end:

section .boot
bits 32

_start:
    mov esp, stack_top
    mov edi, ebx

    call check_multiboot
    call check_cpuid
    call check_long_mode

    call setup_page_tables
    call enable_paging

    lgdt [gdt64.pointer]
    jmp gdt64.code_segment:long_mode_start

    hlt

check_multiboot:
    cmp eax, 0x36d76289
    jne .no_multiboot
    ret
.no_multiboot:
    mov al, "0"
    jmp error

check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid
    ret
.no_cpuid:
    mov al, "1"
    jmp error

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    ret
.no_long_mode:
    mov al, "2"
    jmp error

setup_page_tables:
    mov eax, page_table_l3
    or eax, 0b11
    mov [page_table_l4], eax

    mov eax, page_table_l2
    or eax, 0b11
    mov [page_table_l3], eax

    mov ecx, 0
.loop:
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011
    mov [page_table_l2 + ecx * 8], eax

    inc ecx
    cmp ecx, 512
    jne .loop

    ret

enable_paging:
    mov eax, page_table_l4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ret

error:
    mov dword [0xb8000], 0x4f524f45
    mov dword [0xb8004], 0x4f3a4f52
    mov dword [0xb8008], 0x4f204f20
    mov byte  [0xb800a], al
    hlt

bits 64
long_mode_start:
    mov ax, gdt64.data_segment
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call kernel_main
    hlt

section .bss.boot
align 4096
page_table_l4:
    resb 4096
page_table_l3:
    resb 4096
page_table_l2:
    resb 4096
align 16
stack_bottom:
    resb 16384
stack_top:

section .boot
align 8
gdt64:
    dq 0
.code_segment: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
.data_segment: equ $ - gdt64
    dq (1 << 44) | (1 << 47)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64
