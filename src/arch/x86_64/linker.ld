ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS
{
    . = 1M;

    .boot :
    {
        KEEP(*(.multiboot))
        KEEP(*(.boot))
    }

    .boot.bss (NOLOAD) :
    {
        *(.bss.boot)
    }

    . += KERNEL_VMA;

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA)
    {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA)
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
    }
}
