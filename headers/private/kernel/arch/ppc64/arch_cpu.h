/*
 * PowerPC 64-bit architecture definitions for IBM PPC970-class CPUs.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_PPC64_CPU_H
#define _KERNEL_ARCH_PPC64_CPU_H

#include <arch/ppc/arch_thread_types.h>
#include <kernel.h>

#define CPU_MAX_CACHE_LEVEL 8
#define CACHE_LINE_SIZE 128

#define arch_cpu_enable_user_access()
#define arch_cpu_disable_user_access()

/* PPC64 exception frame.  The layout is intentionally 64-bit aligned so
 * assembly exception entry can spill GPRs without mixed-width offsets. */
struct iframe {
	uint64 vector;
	uint64 srr0;
	uint64 srr1;
	uint64 dar;
	uint64 dsisr;
	uint64 lr;
	uint64 cr;
	uint64 xer;
	uint64 ctr;
	uint64 fpscr;
	uint64 r31;
	uint64 r30;
	uint64 r29;
	uint64 r28;
	uint64 r27;
	uint64 r26;
	uint64 r25;
	uint64 r24;
	uint64 r23;
	uint64 r22;
	uint64 r21;
	uint64 r20;
	uint64 r19;
	uint64 r18;
	uint64 r17;
	uint64 r16;
	uint64 r15;
	uint64 r14;
	uint64 r13;
	uint64 r12;
	uint64 r11;
	uint64 r10;
	uint64 r9;
	uint64 r8;
	uint64 r7;
	uint64 r6;
	uint64 r5;
	uint64 r4;
	uint64 r3;
	uint64 r2;
	uint64 r1;
	uint64 r0;
	uint64 f31;
	uint64 f30;
	uint64 f29;
	uint64 f28;
	uint64 f27;
	uint64 f26;
	uint64 f25;
	uint64 f24;
	uint64 f23;
	uint64 f22;
	uint64 f21;
	uint64 f20;
	uint64 f19;
	uint64 f18;
	uint64 f17;
	uint64 f16;
	uint64 f15;
	uint64 f14;
	uint64 f13;
	uint64 f12;
	uint64 f11;
	uint64 f10;
	uint64 f9;
	uint64 f8;
	uint64 f7;
	uint64 f6;
	uint64 f5;
	uint64 f4;
	uint64 f3;
	uint64 f2;
	uint64 f1;
	uint64 f0;
};

enum machine_state {
	MSR_EXCEPTIONS_ENABLED = 1ULL << 48,
	MSR_PRIVILEGE_LEVEL = 1ULL << 49,
	MSR_FP_AVAILABLE = 1ULL << 50,
	MSR_MACHINE_CHECK_ENABLED = 1ULL << 51,
	MSR_EXCEPTION_PREFIX = 1ULL << 43,
	MSR_INST_ADDRESS_TRANSLATION = 1ULL << 58,
	MSR_DATA_ADDRESS_TRANSLATION = 1ULL << 59
};

#define eieio() asm volatile("eieio")
#define isync() asm volatile("isync")
#define tlbsync() asm volatile("tlbsync")
#define ppc_sync() asm volatile("sync")
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

/* SLB primitives used by the PPC970 hash-MMU bootstrap. */
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
