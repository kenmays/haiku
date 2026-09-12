/* PowerPC 64-bit signal register context. */
#ifndef _ARCH_PPC64_SIGNAL_H_
#define _ARCH_PPC64_SIGNAL_H_

struct vregs {
	unsigned long pc;
	unsigned long r0, r1, r2;
	unsigned long r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
	double f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13;
	unsigned long filler1;
	unsigned long fpscr;
	unsigned long ctr, xer, cr, msr, lr;
};

#endif
