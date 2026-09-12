/* PPC970 64-bit CPU primitives. */
#define FUNCTION(x) .global x; .type x,@function; x
#include "syscall_numbers.h"
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
	andi. %r3,%r3,63
	oris %r3,%r3,0x0800
	or %r3,%r3,%r4
	sldi %r5,%r5,12
	ori %r5,%r5,0x0400
	slbmte %r5,%r3
	isync
	blr
FUNCTION(ppc64_slb_invalidate_esid): slbie %r3; sync; isync; blr
FUNCTION(__eieio): eieio; blr
FUNCTION(ppc_kernel_thread_root):
	mtctr %r14; mr %r3,%r15; bctrl; b kernel_thread_exit
FUNCTION(ppc64_enter_userspace):
	mfmsr %r9
	ori %r10,%r9,0x8000
	ori %r10,%r10,0x2000
	ori %r10,%r10,0x0030
	ori %r10,%r10,0x4000
	mtsrr0 %r3
	mtsrr1 %r10
	mr %r1,%r4
	mr %r3,%r5
	mr %r4,%r6
	mr %r13,%r7
	mtlr %r8
	rfid
	b .
FUNCTION(ppc64_restore_iframe):
	mr %r12,%r3
	lfd %f0,72(%r12); mtfsf 0xff,%f0
	lfd %f0,336(%r12); lfd %f1,344(%r12); lfd %f2,352(%r12); lfd %f3,360(%r12)
	lfd %f4,368(%r12); lfd %f5,376(%r12); lfd %f6,384(%r12); lfd %f7,392(%r12)
	lfd %f8,400(%r12); lfd %f9,408(%r12); lfd %f10,416(%r12); lfd %f11,424(%r12)
	lfd %f12,432(%r12); lfd %f13,440(%r12); lfd %f14,448(%r12); lfd %f15,456(%r12)
	lfd %f16,464(%r12); lfd %f17,472(%r12); lfd %f18,480(%r12); lfd %f19,488(%r12)
	lfd %f20,496(%r12); lfd %f21,504(%r12); lfd %f22,512(%r12); lfd %f23,520(%r12)
	lfd %f24,528(%r12); lfd %f25,536(%r12); lfd %f26,544(%r12); lfd %f27,552(%r12)
	lfd %f28,560(%r12); lfd %f29,568(%r12); lfd %f30,576(%r12); lfd %f31,584(%r12)
	ld %r0,8(%r12); mtspr 26,%r0; ld %r0,16(%r12); mtspr 27,%r0
	ld %r0,24(%r12); mtspr 19,%r0; ld %r0,32(%r12); mtspr 18,%r0
	ld %r0,40(%r12); mtlr %r0; ld %r0,48(%r12); mtcrf 0xff,%r0
	ld %r0,56(%r12); mtxer %r0; ld %r0,64(%r12); mtctr %r0
	ld %r31,80(%r12); ld %r30,88(%r12); ld %r29,96(%r12); ld %r28,104(%r12)
	ld %r27,112(%r12); ld %r26,120(%r12); ld %r25,128(%r12); ld %r24,136(%r12)
	ld %r23,144(%r12); ld %r22,152(%r12); ld %r21,160(%r12); ld %r20,168(%r12)
	ld %r19,176(%r12); ld %r18,184(%r12); ld %r17,192(%r12); ld %r16,200(%r12)
	ld %r15,208(%r12); ld %r14,216(%r12); ld %r13,224(%r12); ld %r11,240(%r12)
	ld %r10,248(%r12); ld %r9,256(%r12); ld %r8,264(%r12); ld %r7,272(%r12)
	ld %r6,280(%r12); ld %r5,288(%r12); ld %r4,296(%r12); ld %r3,304(%r12)
	ld %r2,312(%r12); ld %r1,320(%r12); ld %r0,328(%r12); ld %r12,232(%r12)
	rfid
	b .
FUNCTION(arch_user_thread_exit):
	li %r0,SYSCALL_EXIT_THREAD
	sc
	b .
FUNCTION(ppc_context_switch):
	/* r3 = old arch_thread, r4 = new SP, r5 = new arch_thread. */
	stfd %f0,40(%r3); stfd %f1,48(%r3); stfd %f2,56(%r3); stfd %f3,64(%r3)
	stfd %f4,72(%r3); stfd %f5,80(%r3); stfd %f6,88(%r3); stfd %f7,96(%r3)
	stfd %f8,104(%r3); stfd %f9,112(%r3); stfd %f10,120(%r3); stfd %f11,128(%r3)
	stfd %f12,136(%r3); stfd %f13,144(%r3); stfd %f14,152(%r3); stfd %f15,160(%r3)
	stfd %f16,168(%r3); stfd %f17,176(%r3); stfd %f18,184(%r3); stfd %f19,192(%r3)
	stfd %f20,200(%r3); stfd %f21,208(%r3); stfd %f22,216(%r3); stfd %f23,224(%r3)
	stfd %f24,232(%r3); stfd %f25,240(%r3); stfd %f26,248(%r3); stfd %f27,256(%r3)
	stfd %f28,264(%r3); stfd %f29,272(%r3); stfd %f30,280(%r3); stfd %f31,288(%r3)
	mffs %f0; stfd %f0,296(%r3)
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
	lfd %f0,296(%r5); mtfsf 0xff,%f0
	lfd %f0,40(%r5); lfd %f1,48(%r5); lfd %f2,56(%r5); lfd %f3,64(%r5)
	lfd %f4,72(%r5); lfd %f5,80(%r5); lfd %f6,88(%r5); lfd %f7,96(%r5)
	lfd %f8,104(%r5); lfd %f9,112(%r5); lfd %f10,120(%r5); lfd %f11,128(%r5)
	lfd %f12,136(%r5); lfd %f13,144(%r5); lfd %f14,152(%r5); lfd %f15,160(%r5)
	lfd %f16,168(%r5); lfd %f17,176(%r5); lfd %f18,184(%r5); lfd %f19,192(%r5)
	lfd %f20,200(%r5); lfd %f21,208(%r5); lfd %f22,216(%r5); lfd %f23,224(%r5)
	lfd %f24,232(%r5); lfd %f25,240(%r5); lfd %f26,248(%r5); lfd %f27,256(%r5)
	lfd %f28,264(%r5); lfd %f29,272(%r5); lfd %f30,280(%r5); lfd %f31,288(%r5)
	addi %r1,%r1,192; blr
