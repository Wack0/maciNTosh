OUTPUT_FORMAT("elf32-powerpcle")
OUTPUT_ARCH(powerpc:common)

ENTRY(_start)

SECTIONS {
	. = 0x80900000;
	__executable_start = .;

	.text : ALIGN(8) { *(.text) *(.text.*) }

	.rodata : ALIGN(8) { *(.rodata) *(.rodata.*) }

	.sdata : ALIGN(8) { *(.sdata) *(.sdata.*) }
	.data : ALIGN(8) { *(.data) *(.data.*) }

	. = ALIGN(32);
	__self_end = .;

	__bss_start = .;
	.bss : ALIGN(8) { *(.bss .bss.*) } :NONE = 0
	.sbss : { *(.sbss) }
	__bss_end = .;

	. = ALIGN(32);
	_end = .;
}