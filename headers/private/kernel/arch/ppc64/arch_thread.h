/* PowerPC 64-bit thread helpers. */
#ifndef _KERNEL_ARCH_PPC64_THREAD_H
#define _KERNEL_ARCH_PPC64_THREAD_H

#include <arch/cpu.h>
#include <thread.h>

#ifdef __cplusplus
extern "C" {
#endif

void ppc_push_iframe(struct iframe_stack* stack, struct iframe* frame);
void ppc_pop_iframe(struct iframe_stack* stack);
struct iframe* ppc_get_user_iframe(void);

/* SPRG1 is the current-thread pointer; SPRG2/3 are exception scratch. */
static inline Thread* arch_thread_get_current_thread(void)
{
	Thread* thread;
	asm volatile("mfsprg1 %0" : "=r"(thread));
	return thread;
}
static inline void arch_thread_set_current_thread(Thread* thread)
{
	asm volatile("mtsprg1 %0" :: "r"(thread));
}

#ifdef __cplusplus
}
#endif

#endif
