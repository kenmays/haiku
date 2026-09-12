/* PPC64 low-level interrupt and ABI helpers. */
#define FUNCTION(x) .global x; .type x,@function; x
.text

FUNCTION(arch_int_enable_interrupts):
	mfmsr %r3
	ori %r3,%r3,0x8000
	mtmsrd %r3
	isync
	blr

FUNCTION(arch_int_disable_interrupts):
	mfmsr %r4
	mr %r3,%r4
	li %r5,0x8000
	andc %r4,%r4,%r5
	mtmsrd %r4
	isync
	blr

FUNCTION(arch_int_restore_interrupts):
	mfmsr %r4
	li %r5,0x8000
	andc %r4,%r4,%r5
	and %r5,%r3,%r5
	or %r4,%r4,%r5
	mtmsrd %r4
	isync
	blr

FUNCTION(arch_int_are_interrupts_enabled):
	mfmsr %r3
	andi. %r3,%r3,0x8000
	blr

FUNCTION(reboot):
	b .

FUNCTION(dbg_save_registers):
	blr
