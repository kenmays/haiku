#ifndef _KERNEL_ARCH_PPC64_SMP_H
#define _KERNEL_ARCH_PPC64_SMP_H

#include <kernel.h>
#include <smp.h>

struct ppc_cpu_exception_context {
	void* exception_context;
	uint64 reserved;
	addr_t kernel_stack;
	uint64 scratch0;
	uint64 scratch1;
};

ppc_cpu_exception_context* ppc_get_cpu_exception_context(int cpu);
void ppc_set_current_cpu_exception_context(ppc_cpu_exception_context* context);

#endif
