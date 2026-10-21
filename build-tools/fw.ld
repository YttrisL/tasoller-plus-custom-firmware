/* TASOLLER Plus custom firmware, C build.
 *
 * Linked at 0x4000 behind the bootloader. The vector table is first, the version field sits at
 * 0x4400 where the package tools and the updater look for it, then code and constants. All RAM
 * variables are at fixed stock addresses (see src/state.h), so the linker must not place
 * anything in RAM. */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00004000, LENGTH = 0x00019000
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 0x0001ED48
}

ENTRY(Reset_Handler)

SECTIONS
{
    .vectors 0x00004000 :
    {
        KEEP(*(.vectors))
    } > FLASH

    .version 0x00004400 :
    {
        KEEP(*(.version))
    } > FLASH

    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH

    .ram_unused (NOLOAD) : { *(.data .data.* .bss .bss.* COMMON) } > RAM

    /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) *(.ARM.exidx*) *(.eh_frame*) }
}

ASSERT(SIZEOF(.ram_unused) == 0, "the firmware must not define RAM variables, use the macros in ram.h")
ASSERT(SIZEOF(.vectors) == 0x1b8, "vector table is not 110 words")
ASSERT(SIZEOF(.version) == 0x28, "version field is not 40 bytes")
