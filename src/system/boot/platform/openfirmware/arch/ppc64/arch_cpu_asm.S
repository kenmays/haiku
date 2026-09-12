/* PPC64 Open Firmware boot CPU helpers. */
#define FUNCTION(x) .global x; .type x,@function; x
.text

FUNCTION(ppc64_boot_get_pvr):
	mfpvr	%r3
	blr

FUNCTION(ppc64_boot_get_msr):
	mfmsr	%r3
	blr

/* Disable IR and DR while preserving every other MSR bit.  On PPC64 the
 * assembler's bit numbering for logical/rotate instructions is MSB-first;
 * using rldicr with a mask generated from -1 would be unnecessarily subtle,
 * so construct the two-bit clear mask explicitly and invert it. */
FUNCTION(ppc64_boot_disable_translation):
	mfmsr	%r3
	li	%r4, 0
	ori	%r4, %r4, 0x0c00
	sldi	%r4, %r4, 48
	nor	%r4, %r4, %r4
	and	%r3, %r3, %r4
	mtmsrd	%r3
	isync
	blr
