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
	mfsprg0 %r13
	std %r1,32(%r13)
	ld %r1,16(%r13)
	addi %r1,%r1,-640
	mr %r4,%r1

	std %r0,0(%r4)
	mfspr %r0,26; std %r0,8(%r4)
	mfspr %r0,27; std %r0,16(%r4)
	mfspr %r0,19; std %r0,24(%r4)
	mfspr %r0,18; std %r0,32(%r4)
	mflr %r0; std %r0,40(%r4)
	mfcr %r0; std %r0,48(%r4)
	mfxer %r0; std %r0,56(%r4)
	mfctr %r0; std %r0,64(%r4)

	/* The exception handler may be entered with MSR[FP] clear. Enable the
	 * FPU while in the kernel; SRR1 is restored by rfid on exit. */
	mfmsr %r0
	ori %r0,%r0,0x2000
	mtmsrd %r0
	isync

	stfd %f0,336(%r4)
	stfd %f1,344(%r4);  stfd %f2,352(%r4);  stfd %f3,360(%r4)
	stfd %f4,368(%r4);  stfd %f5,376(%r4);  stfd %f6,384(%r4)
	stfd %f7,392(%r4);  stfd %f8,400(%r4);  stfd %f9,408(%r4)
	stfd %f10,416(%r4); stfd %f11,424(%r4); stfd %f12,432(%r4)
	stfd %f13,440(%r4); stfd %f14,448(%r4); stfd %f15,456(%r4)
	stfd %f16,464(%r4); stfd %f17,472(%r4); stfd %f18,480(%r4)
	stfd %f19,488(%r4); stfd %f20,496(%r4); stfd %f21,504(%r4)
	stfd %f22,512(%r4); stfd %f23,520(%r4); stfd %f24,528(%r4)
	stfd %f25,536(%r4); stfd %f26,544(%r4); stfd %f27,552(%r4)
	stfd %f28,560(%r4); stfd %f29,568(%r4); stfd %f30,576(%r4)
	stfd %f31,584(%r4)
	mffs %f0
	stfd %f0,72(%r4)
	lfd %f0,336(%r4)

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
	bl ppc64_exception_entry

	lfd %f0,72(%r1); mtfsf 0xff,%f0
	lfd %f0,336(%r1); lfd %f1,344(%r1); lfd %f2,352(%r1); lfd %f3,360(%r1)
	lfd %f4,368(%r1); lfd %f5,376(%r1); lfd %f6,384(%r1); lfd %f7,392(%r1)
	lfd %f8,400(%r1); lfd %f9,408(%r1); lfd %f10,416(%r1); lfd %f11,424(%r1)
	lfd %f12,432(%r1); lfd %f13,440(%r1); lfd %f14,448(%r1); lfd %f15,456(%r1)
	lfd %f16,464(%r1); lfd %f17,472(%r1); lfd %f18,480(%r1); lfd %f19,488(%r1)
	lfd %f20,496(%r1); lfd %f21,504(%r1); lfd %f22,512(%r1); lfd %f23,520(%r1)
	lfd %f24,528(%r1); lfd %f25,536(%r1); lfd %f26,544(%r1); lfd %f27,552(%r1)
	lfd %f28,560(%r1); lfd %f29,568(%r1); lfd %f30,576(%r1); lfd %f31,584(%r1)

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
