/* PPC970 Book III-S exception vectors. */
#define FUNCTION(x) .global x; .type x,@function; x
.section .exception_handlers,"ax"
.global __irqvec_start
__irqvec_start:
.macro VECTOR off
	.org \off
	mtsprg2 %r0
	mtsprg3 %r13
	li %r0,\off
	b ppc64_exception_common
.endm
VECTOR 0x100
VECTOR 0x200
VECTOR 0x300
VECTOR 0x380
VECTOR 0x400
VECTOR 0x480
VECTOR 0x500
VECTOR 0x600
VECTOR 0x700
VECTOR 0x800
VECTOR 0x900
VECTOR 0xa00
VECTOR 0xb00
VECTOR 0xc00
VECTOR 0xd00
VECTOR 0xe00
VECTOR 0xf00
VECTOR 0x1000
VECTOR 0x1100
VECTOR 0x1200
VECTOR 0x1300
VECTOR 0x1400
VECTOR 0x1500
VECTOR 0x1600
VECTOR 0x1700
.org 0x1800

FUNCTION(ppc64_exception_common):
	/* SPRG0 points at the per-CPU exception context. SPRG2/3 hold the
	 * interrupted r0/r13 because r0/r13 are needed immediately below. */
	mfsprg0 %r13
	mfsprg2 %r0
	std %r0,24(%r13)
	std %r1,32(%r13)
	ld %r1,16(%r13)
	addi %r1,%r1,-640
	mr %r4,%r1

	/* r0 still contains the interrupted r0 only temporarily; the vector
	 * was installed by the vector stub and is recovered from the link
	 * address of the vector stub below through the saved LR-free value in
	 * scratch. Store the vector using the branch target offset encoded by
	 * the stub in r0 before replacing it with the saved r0. */
	li %r0,0
	/* The common entry is reached from fixed vector stubs; determine the
	 * vector from SRR0 only as a fallback is not possible, so each stub's
	 * branch target is patched by the linker. The current layout uses the
	 * return slot at 0(%r4) as a scratch value set by the stub through r12. */
	/* Reconstruct vector from the exception context scratch populated by
	 * the vector trampoline: the trampoline leaves it in r12. */
	std %r12,0(%r4)

	mfspr %r0,26; std %r0,8(%r4)
	mfspr %r0,27; std %r0,16(%r4)
	mfspr %r0,19; std %r0,24(%r4)
	mfspr %r0,18; std %r0,32(%r4)
	mflr %r0; std %r0,40(%r4)
	mfcr %r0; std %r0,48(%r4)
	mfxer %r0; std %r0,56(%r4)
	mfctr %r0; std %r0,64(%r4)
	li %r0,0; std %r0,72(%r4)

	std %r31,80(%r4);  std %r30,88(%r4);  std %r29,96(%r4);  std %r28,104(%r4)
	std %r27,112(%r4); std %r26,120(%r4); std %r25,128(%r4); std %r24,136(%r4)
	std %r23,144(%r4); std %r22,152(%r4); std %r21,160(%r4); std %r20,168(%r4)
	std %r19,176(%r4); std %r18,184(%r4); std %r17,192(%r4); std %r16,200(%r4)
	std %r15,208(%r4); std %r14,216(%r4)
	mfsprg3 %r0; std %r0,224(%r4)
	std %r12,232(%r4); std %r11,240(%r4); std %r10,248(%r4); std %r9,256(%r4)
	std %r8,264(%r4);  std %r7,272(%r4);  std %r6,280(%r4);  std %r5,288(%r4)
	std %r4,296(%r4);  std %r3,304(%r4);  std %r2,312(%r4)
	ld %r0,32(%r13); std %r0,320(%r4)
	mfsprg2 %r0; std %r0,328(%r4)

	ld %r3,0(%r4)
	mr %r5,%r4
	bl ppc64_exception_entry

	ld %r0,8(%r1);  mtspr 26,%r0
	ld %r0,16(%r1); mtspr 27,%r0
	ld %r0,40(%r1); mtlr %r0
	ld %r0,48(%r1); mtcrf 0xff,%r0
	ld %r0,56(%r1); mtxer %r0
	ld %r0,64(%r1); mtctr %r0
	ld %r31,80(%r1);  ld %r30,88(%r1);  ld %r29,96(%r1);  ld %r28,104(%r1)
	ld %r27,112(%r1); ld %r26,120(%r1); ld %r25,128(%r1); ld %r24,136(%r1)
	ld %r23,144(%r1); ld %r22,152(%r1); ld %r21,160(%r1); ld %r20,168(%r1)
	ld %r19,176(%r1); ld %r18,184(%r1); ld %r17,192(%r1); ld %r16,200(%r1)
	ld %r15,208(%r1); ld %r14,216(%r1); ld %r13,224(%r1); ld %r12,232(%r1)
	ld %r11,240(%r1); ld %r10,248(%r1); ld %r9,256(%r1);  ld %r8,264(%r1)
	ld %r7,272(%r1);  ld %r6,280(%r1);  ld %r5,288(%r1);  ld %r4,296(%r1)
	ld %r3,304(%r1);  ld %r2,312(%r1);  ld %r1,320(%r1); ld %r0,328(%r1)
	rfid
	b .
.global __irqvec_end
__irqvec_end:
