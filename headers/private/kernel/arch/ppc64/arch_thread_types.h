/* PowerPC 64-bit architecture thread types. */
#ifndef KERNEL_ARCH_PPC64_THREAD_TYPES_H
#define KERNEL_ARCH_PPC64_THREAD_TYPES_H

#include <kernel.h>

#define IFRAME_TRACE_DEPTH 4

struct iframe {
	uint64 vector, srr0, srr1, dar, dsisr, lr, cr, xer, ctr, fpscr;
	uint64 r31, r30, r29, r28, r27, r26, r25, r24, r23, r22, r21, r20,
		r19, r18, r17, r16, r15, r14, r13, r12, r11, r10, r9, r8, r7, r6,
		r5, r4, r3, r2, r1, r0;
	double f31, f30, f29, f28, f27, f26, f25, f24, f23, f22, f21, f20,
		f19, f18, f17, f16, f15, f14, f13, f12, f11, f10, f9, f8, f7, f6,
		f5, f4, f3, f2, f1, f0;
};

struct iframe_stack {
	struct iframe* frames[IFRAME_TRACE_DEPTH];
	int32 index;
};

struct arch_thread {
	void* sp;
	void* interrupt_stack;
	struct iframe_stack iframes;
};

struct arch_team { char dummy; };
struct arch_fork_arg { struct iframe frame; };

#endif
