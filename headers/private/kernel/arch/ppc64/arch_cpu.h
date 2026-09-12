/* PowerPC 64-bit architecture definitions for IBM PPC970-class CPUs. */
#ifndef _KERNEL_ARCH_PPC64_CPU_H
#define _KERNEL_ARCH_PPC64_CPU_H

#include <arch/ppc64/arch_thread_types.h>
#include <kernel.h>

#define CPU_MAX_CACHE_LEVEL 8
#define CACHE_LINE_SIZE 128
#define arch_cpu_enable_user_access()
#define arch_cpu_disable_user_access()

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
extern void set_msr(uint64 value);
extern uint32 get_pvr(void);
extern uint64 get_time_base(void);
extern void ppc64_slb_invalidate(void);
extern void ppc64_slb_insert(uint64 slot, uint64 esid, uint64 vsid);
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
