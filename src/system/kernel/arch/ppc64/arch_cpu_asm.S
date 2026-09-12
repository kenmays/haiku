/* PPC970 64-bit CPU primitives. */
#define FUNCTION(x) .global x; .type x,@function; x
.text
FUNCTION(get_sdr1): mfspr %r3,25; blr
FUNCTION(set_sdr1): mtspr 25,%r3; sync; blr
FUNCTION(get_msr): mfmsr %r3; blr
FUNCTION(set_msr): mtmsrd %r3; isync; blr
FUNCTION(get_pvr): mfpvr %r3; blr
FUNCTION(get_time_base):
1: mftbu %r4; mftb %r3; mftbu %r5; cmpw %r4,%r5; bne 1b
	sldi %r4,%r4,32; or %r3,%r3,%r4; blr
FUNCTION(ppc64_slb_invalidate): slbia; sync; isync; blr
FUNCTION(ppc64_slb_insert):
	/* r3 = 256 MiB ESID, r4 = VSID number. */
	srdi %r3,%r3,28
	sldi %r3,%r3,28
	lis %r5,0x0800
	or %r3,%r3,%r5
	sldi %r4,%r4,12
	ori %r4,%r4,0x0400
	slbmte %r4,%r3
	isync
	blr
FUNCTION(ppc64_slb_invalidate_esid): slbie %r3; sync; isync; blr
FUNCTION(__eieio): eieio; blr
FUNCTION(ppc_kernel_thread_root):
	mtctr %r14; mr %r3,%r15; bctrl; b kernel_thread_exit
FUNCTION(ppc_context_switch):
	mflr %r0; std %r0,0(%r1); mfcr %r0; std %r0,8(%r1); stdu %r1,-192(%r1)
	std %r2,16(%r1); std %r13,24(%r1); std %r14,32(%r1); std %r15,40(%r1)
	std %r16,48(%r1); std %r17,56(%r1); std %r18,64(%r1); std %r19,72(%r1)
	std %r20,80(%r1); std %r21,88(%r1); std %r22,96(%r1); std %r23,104(%r1)
	std %r24,112(%r1); std %r25,120(%r1); std %r26,128(%r1); std %r27,136(%r1)
	std %r28,144(%r1); std %r29,152(%r1); std %r30,160(%r1); std %r31,168(%r1)
	std %r1,0(%r3); mr %r1,%r4; ld %r0,0(%r1); mtlr %r0; ld %r0,8(%r1); mtcrf 0xff,%r0
	ld %r2,16(%r1); ld %r13,24(%r1); ld %r14,32(%r1); ld %r15,40(%r1)
	ld %r16,48(%r1); ld %r17,56(%r1); ld %r18,64(%r1); ld %r19,72(%r1)
	ld %r20,80(%r1); ld %r21,88(%r1); ld %r22,96(%r1); ld %r23,104(%r1)
	ld %r24,112(%r1); ld %r25,120(%r1); ld %r26,128(%r1); ld %r27,136(%r1)
	ld %r28,144(%r1); ld %r29,152(%r1); ld %r30,160(%r1); ld %r31,168(%r1)
	addi %r1,%r1,192; blr
