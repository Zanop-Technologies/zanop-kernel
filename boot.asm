; =====================================================================
; Zanop Kernel — boot.asm
; Multiboot2-compliant entry point + 32-bit -> 64-bit long-mode switch.
; Identity-maps the first 2MiB with a single 2MB page, enables PAE and
; long mode, then far-jumps into 64-bit code and calls kernel_main.
; =====================================================================

global _start
extern kernel_main

section .multiboot
align 8
multiboot_header:
    dd 0xe85250d6                ; magic
    dd 0                         ; architecture: 0 = i386/x86
    dd multiboot_header_end - multiboot_header
    dd -(0xe85250d6 + 0 + (multiboot_header_end - multiboot_header))
    ; end tag
    dw 0
    dw 0
    dd 8
multiboot_header_end:

section .boot
bits 32

_start:
    mov esp, stack_top
    mov edi, ebx                 ; save multiboot info pointer (arg1 for later)

    call check_multiboot
    call check_cpuid
    call check_long_mode

    call setup_page_tables
    call enable_paging

    lgdt [gdt64.pointer]
    jmp gdt64.code_segment:long_mode_start

    hlt

; ----- sanity checks -----------------------------------------------

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

; ----- paging setup (identity-map first 2MiB via one 2MB page) -----

setup_page_tables:
    mov eax, page_table_l3
    or eax, 0b11                 ; present + writable
    mov [page_table_l4], eax

    mov eax, page_table_l2
    or eax, 0b11
    mov [page_table_l3], eax

    mov ecx, 0
.loop:
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011           ; present + writable + huge (2MB) page
    mov [page_table_l2 + ecx * 8], eax

    inc ecx
    cmp ecx, 512
    jne .loop

    ret

enable_paging:
    mov eax, page_table_l4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5                ; PAE
    mov cr4, eax

    mov ecx, 0xC0000080           ; EFER MSR
    rdmsr
    or eax, 1 << 8                ; long mode enable (LME)
    wrmsr

    mov eax, cr0
    or eax, 1 << 31               ; enable paging
    mov cr0, eax

    ret

; ----- error path: prints "ERR: X" to VGA text buffer and halts -----

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

; ----- data ----------------------------------------------------------

section .bss
align 4096
page_table_l4:
    resb 4096
page_table_l3:
    resb 4096
page_table_l2:
    resb 4096
align 16
stack_bottom:
    resb 16384                    ; 16 KiB stack
stack_top:

section .rodata
align 8
gdt64:
    dq 0                          ; null descriptor
.code_segment: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53) ; code, present, 64-bit
.data_segment: equ $ - gdt64
    dq (1 << 44) | (1 << 47)                          ; data, present
.pointer:
    dw $ - gdt64 - 1
    dq gdt64