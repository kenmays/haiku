/* PPC64 Open Firmware boot CPU helpers. */
#define FUNCTION(x) .global x; .type x,@function; x
.text

FUNCTION(ppc64_boot_get_pvr):
	mfpvr	%r3
	blr

FUNCTION(ppc64_boot_get_msr):
	mfmsr	%r3
	blr

FUNCTION(ppc64_boot_disable_translation):
	mfmsr	%r3
	li	%r4, 0
	/* Clear IR/DR in the 64-bit MSR. */
	rldicr	%r4, %r4, 0, 63
	li	%r4, 0
	mtmsrd	%r4
	isync
	blr
