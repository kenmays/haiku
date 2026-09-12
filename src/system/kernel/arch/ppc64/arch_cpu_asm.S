/*
 * PPC970 64-bit CPU primitives.
 * Distributed under the terms of the MIT License.
 */
#define FUNCTION(x) .global x; .type x,@function; x

.text

FUNCTION(get_sdr1):
	mfspr	%r3, 25
	blr

FUNCTION(set_sdr1):
	mtspr	25, %r3
	sync
	blr

FUNCTION(get_msr):
	mfmsr	%r3
	blr

FUNCTION(set_msr):
	mtmsrd	%r3
	isync
	blr

FUNCTION(get_pvr):
	mfpvr	%r3
	blr

FUNCTION(get_time_base):
1:	mftbu	%r4
	mftb	%r3
	mftbu	%r5
	cmpw	%r4, %r5
	bne	1b
	sldi	%r4, %r4, 32
	or	%r3, %r3, %r4
	blr

/* Clear the SLB.  The PPC970 SLB is the required replacement for the
 * classic 32-bit segment-register interface. */
FUNCTION(ppc64_slb_invalidate):
	slbia
	sync
	isync
	blr

/* r3 = ESID value, r4 = VSID value. */
FUNCTION(ppc64_slb_insert):
	slbmte	%r4, %r3
	isync
	blr

/* r3 = effective address / ESID to invalidate. */
FUNCTION(ppc64_slb_invalidate_esid):
	slbie	%r3
	sync
	isync
	blr

/*
 * __eieio is retained for generic kernel code that references the historical
 * PPC symbol directly.
 */
FUNCTION(__eieio):
	eieio
	blr
