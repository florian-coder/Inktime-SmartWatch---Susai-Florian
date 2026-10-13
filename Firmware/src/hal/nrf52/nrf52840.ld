/* nRF52840 (QIAA): 1 MiB flash, 256 KiB RAM. No SoftDevice: the image starts at 0. */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
    UICR_NFCPINS (r) : ORIGIN = 0x1000120C, LENGTH = 4
}

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 0x2000; /* 8 KiB reserved for the main stack */

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* newlib's _sbrk (nosys) starts the heap here; the firmware does not use malloc. */
    end = _ebss;
    PROVIDE(_end = _ebss);

    /* Programmed together with the image (HEX/ELF only, stripped from the .bin). */
    .uicr_nfcpins :
    {
        KEEP(*(.uicr_nfcpins))
    } > UICR_NFCPINS

    ASSERT(_ebss + _min_stack <= _estack, "RAM overflow: not enough room for the stack")
}
