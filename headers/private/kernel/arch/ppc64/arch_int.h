/* PowerPC 64-bit exception context. */
#ifndef _KERNEL_ARCH_PPC64_INT_H
#define _KERNEL_ARCH_PPC64_INT_H

#include <SupportDefs.h>

#define NUM_IO_VECTORS 256

struct ppc_cpu_exception_context {
	void* kernel_handle_exception;
	void* exception_context;
	void* kernel_stack;
	uint64 scratch[8];
};

#ifdef __cplusplus
extern "C" {
#endif
struct ppc_cpu_exception_context* ppc_get_cpu_exception_context(int cpu);
void ppc_set_current_cpu_exception_context(struct ppc_cpu_exception_context* context);
#ifdef __cplusplus
}
#endif

#endif
