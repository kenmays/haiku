OUTPUT_FORMAT("elf64-powerpc", "elf64-powerpc", "elf64-powerpc")
OUTPUT_ARCH(powerpc:common64)
ENTRY(_start)
SEARCH_DIR("libgcc");
SECTIONS
{
	/* Keep the initial image in the low 4 GiB RMO so Open Firmware can
	 * enter it before the final hash-MMU mapping is established. */
	. = 0x0000000080000000 + SIZEOF_HEADERS;

	.interp : { *(.interp) }
	.hash : { *(.hash) }
	.dynsym : { *(.dynsym) }
	.dynstr : { *(.dynstr) }
	.gnu.version : { *(.gnu.version) }
	.gnu.version_d : { *(.gnu.version_d) }
	.gnu.version_r : { *(.gnu.version_r) }
	.rela.dyn : { *(.rela.init) *(.rela.text .rela.text.*) *(.rela.rodata .rela.rodata.*)
		*(.rela.data .rela.data.*) *(.rela.got) *(.rela.bss .rela.bss.*) }
	.init : { *(.init) } =0x60000000
	.text : { *(.text .stub .text.* .gnu.linkonce.t.*) } =0x60000000
	.rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) }
	.rodata1 : { *(.rodata1) }

	. = ALIGN(0x1000) + 0x1000 + (. & (0x1000 - 1));
	__ctor_list = .;
	.ctors : { *(.ctors) }
	__ctor_end = .;
	.dynamic : { *(.dynamic) }
	__data_start = .;
	.data : { *(.data .gnu.linkonce.d.*) }
	.got : { *(.got.plt) *(.got) }
	.sdata : { *(.sdata .sdata.* .gnu.linkonce.s.*) }
	__bss_start = .;
	.sbss : { *(.sbss .sbss.* .gnu.linkonce.sb.*) }
	.bss : { *(.bss .bss.* .gnu.linkonce.b.*) . = ALIGN(0x1000); }
	_end = .;

	INCLUDE debuginfo.ld
	/DISCARD/ : { *(.comment .note .eh_frame .dtors .debug_*) }
}
