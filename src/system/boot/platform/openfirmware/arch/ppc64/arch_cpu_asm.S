/* PPC64 Open Firmware boot CPU helpers. */
#define FUNCTION(x) .global x; .type x,@function; x
.text

FUNCTION(ppc64_boot_get_pvr):
	mfpvr	%r3
	blr

FUNCTION(ppc64_boot_get_msr):
	mfmsr	%r3
	blr

/* Return the current MSR with instruction/data translation disabled.
 * PPC970 Book III-S MSR uses architectural MSB-first bit numbering;
 * assembler bit 58 is IR and bit 59 is DR. */
FUNCTION(ppc64_boot_disable_translation):
	mfmsr	%r3
	li	%r4, 0
	ori	%r4, %r4, 0
	/* rldimi clears IR/DR in the value copied from r3 below. */
	rldicl	%r4, %r3, 0, 64
	rldimi	%r4, %r3, 0, 5
	/* Clear architectural IR and DR bits (MSR bit positions 58/59). */
	rldicl	%r4, %r4, 0, 6
	mtmsrd	%r4
	isync
	blr
