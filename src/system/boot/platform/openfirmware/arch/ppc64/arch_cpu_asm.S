/* PPC64 Open Firmware boot CPU helpers. */
#define FUNCTION(x) .global x; .type x,@function; x
.text

FUNCTION(ppc64_boot_get_pvr):
	mfpvr	%r3
	blr

FUNCTION(ppc64_boot_get_msr):
	mfmsr	%r3
	blr

/* Disable instruction/data relocation while preserving all other MSR state. */
FUNCTION(ppc64_boot_disable_translation):
	mfmsr	%r3
	li	%r4, 0x30
	nor	%r4, %r4, %r4
	and	%r3, %r3, %r4
	mtmsrd	%r3
	isync
	blr
