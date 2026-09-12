/* PPC970 SMP and OpenPIC IPI delivery. */
#include <KernelExport.h>
#include <interrupts.h>
#include <boot/stage2.h>
#include <arch/smp.h>
#include <arch/int.h>
#include <debug.h>
#include <smp.h>
#include <platform/apple_g5/g5_mpic.h>
static ppc_cpu_exception_context sExceptionContexts[SMP_MAX_CPUS];
static const uint8 kIpiVector=0x20;
ppc_cpu_exception_context* ppc_get_cpu_exception_context(int cpu){return cpu>=0&&cpu<SMP_MAX_CPUS?&sExceptionContexts[cpu]:NULL;}
void ppc_set_current_cpu_exception_context(ppc_cpu_exception_context* c){asm volatile("mtsprg0 %0"::"r"(c));}
status_t arch_smp_init(kernel_args*){return AppleG5::mpic_init();}
status_t arch_smp_per_cpu_init(kernel_args*,int32 cpu){ppc_cpu_exception_context*c=ppc_get_cpu_exception_context(cpu);if(!c)return B_BAD_VALUE;c->exception_context=c;ppc_set_current_cpu_exception_context(c);return AppleG5::mpic_init_per_cpu(cpu);}
void arch_smp_send_ici(int32 cpu){#if KDEBUG
if(are_interrupts_enabled())panic("arch_smp_send_ici: interrupts enabled");
#endif
AppleG5::mpic_send_ipi(cpu,kIpiVector);}
void arch_smp_send_multicast_ici(CPUSet&){#if KDEBUG
if(are_interrupts_enabled())panic("arch_smp_send_multicast_ici: interrupts enabled");
#endif
arch_smp_send_broadcast_ici();}
void arch_smp_send_broadcast_ici(){#if KDEBUG
if(are_interrupts_enabled())panic("arch_smp_send_broadcast_ici: interrupts enabled");
#endif
int32 current=smp_get_current_cpu();for(int32 cpu=0;cpu<smp_get_num_cpus();cpu++)if(cpu!=current)AppleG5::mpic_send_ipi(cpu,kIpiVector);}
