OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)

ENTRY(StartAixCall)

SECTIONS {
	. = 0x00050000;
	__text_start = .;

	.text : { *(.text) *(.text.*) *(.rodata) KEEP(*(.rodata.StartAixCall)); *(.rodata.*) }

	. = ALIGN(4);

	.sdata : { *(.sdata) *(.sdata.*) }
	.data : { *(.data) *(.data.*) }

	. = ALIGN(32);
	__self_end = .;

	__bss_start = .;
	.bss : { *(.bss .bss.*) } :NONE = 0
	.sbss : { *(.sbss) }
	__bss_end = .;

	. = ALIGN(32);
	_end = .;
}