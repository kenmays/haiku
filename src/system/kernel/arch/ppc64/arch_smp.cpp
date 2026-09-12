/* PPC970 SMP bootstrap. Full U3 MPIC IPI delivery is platform-specific;
 * this layer establishes the per-CPU exception context and leaves interrupt
 * routing to the Apple G5 platform driver. */
#include <KernelExport.h>
#include <interrupts.h>
#include <boot/stage2.h>
#include <arch/smp.h>
#include <arch/int.h>
#include <debug.h>

static ppc_cpu_exception_context sExceptionContexts[SMP_MAX_CPUS];

ppc_cpu_exception_context* ppc_get_cpu_exception_context(int cpu)
{
	return cpu >= 0 && cpu < SMP_MAX_CPUS ? &sExceptionContexts[cpu] : NULL;
}

void ppc_set_current_cpu_exception_context(ppc_cpu_exception_context* context)
{
	asm volatile("mtsprg0 %0" :: "r"(context));
}

status_t arch_smp_init(kernel_args*) { return B_OK; }

status_t arch_smp_per_cpu_init(kernel_args*, int32 cpu)
{
	ppc_cpu_exception_context* context = ppc_get_cpu_exception_context(cpu);
	if (context == NULL)
		return B_BAD_VALUE;
	context->exception_context = context;
	ppc_set_current_cpu_exception_context(context);
	return B_OK;
}

void arch_smp_send_ici(int32) { panic("ppc64: MPIC ICI not initialized"); }
void arch_smp_send_multicast_ici(CPUSet&) { panic("ppc64: MPIC ICI not initialized"); }
void arch_smp_send_broadcast_ici() { panic("ppc64: MPIC ICI not initialized"); }
