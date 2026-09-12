/*
 * PowerPC 64-bit architecture definitions for IBM PPC970-class CPUs.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_PPC64_CPU_H
#define _KERNEL_ARCH_PPC64_CPU_H

#include <arch/ppc64/arch_thread_types.h>
#include <kernel.h>

#define CPU_MAX_CACHE_LEVEL 8
#define CACHE_LINE_SIZE 128
#define arch_cpu_enable_user_access()
#define arch_cpu_disable_user_access()

struct iframe {
	uint64 vector, srr0, srr1, dar, dsisr, lr, cr, xer, ctr, fpscr;
	uint64 r31, r30, r29, r28, r27, r26, r25, r24, r23, r22, r21, r20,
		r19, r18, r17, r16, r15, r14, r13, r12, r11, r10, r9, r8, r7, r6,
		r5, r4, r3, r2, r1, r0;
	double f31, f30, f29, f28, f27, f26, f25, f24, f23, f22, f21, f20,
		f19, f18, f17, f16, f15, f14, f13, f12, f11, f10, f9, f8, f7, f6,
		f5, f4, f3, f2, f1, f0;
};

enum machine_state {
	MSR_EXCEPTIONS_ENABLED = 1ULL << 15,
	MSR_PRIVILEGE_LEVEL = 1ULL << 14,
	MSR_FP_AVAILABLE = 1ULL << 13,
	MSR_MACHINE_CHECK_ENABLED = 1ULL << 12,
	MSR_EXCEPTION_PREFIX = 1ULL << 6,
	MSR_INST_ADDRESS_TRANSLATION = 1ULL << 5,
	MSR_DATA_ADDRESS_TRANSLATION = 1ULL << 4,
	MSR_RECOVERABLE_EXCEPTION = 1ULL << 1,
	MSR_64BIT = 1ULL << 63
};

#define eieio() asm volatile("eieio" ::: "memory")
#define isync() asm volatile("isync" ::: "memory")
#define tlbsync() asm volatile("tlbsync" ::: "memory")
#define ppc_sync() asm volatile("sync" ::: "memory")
#define slbia() asm volatile("slbia" ::: "memory")
#define tlbia() asm volatile("tlbia" ::: "memory")
#define tlbie(addr) asm volatile("tlbie %0" :: "r" (addr) : "memory")
#define SRH_very_low() asm volatile("or 31,31,31")
#define SRH_low() asm volatile("or 1,1,1")
#define SRH_medium_low() asm volatile("or 6,6,6")
#define SRH_medium() asm volatile("or 2,2,2")
#define SRH_medium_high() asm volatile("or 5,5,5")
#define SRH_high() asm volatile("or 3,3,3")

typedef struct arch_cpu_info { int null; } arch_cpu_info;

#ifdef __cplusplus
extern "C" {
#endif
extern uint64 get_sdr1(void);
extern void set_sdr1(uint64 value);
extern uint64 get_msr(void);
extern uint64 set_msr(uint64 value);
extern uint32 get_pvr(void);
extern uint64 get_time_base(void);
extern void ppc64_slb_invalidate(void);
extern void ppc64_slb_insert(uint64 esid, uint64 vsid);
extern void ppc64_slb_invalidate_esid(uint64 esid);
extern void ppc_context_switch(void **_oldStackPointer, void *newStackPointer);
extern bool ppc_set_fault_handler(addr_t *handlerLocation, addr_t handler)
	__attribute__((noinline));
#ifdef __cplusplus
}
#endif

static inline void arch_cpu_pause(void) { SRH_very_low(); }
static inline void arch_cpu_idle(void) { asm volatile("or 27,27,27"); }

#endif
