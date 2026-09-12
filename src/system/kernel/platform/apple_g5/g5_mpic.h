/* Apple Power Mac G5 U3/U3H OpenPIC interrupt controller. */
#ifndef KERNEL_PLATFORM_APPLE_G5_MPIC_H
#define KERNEL_PLATFORM_APPLE_G5_MPIC_H

#include <SupportDefs.h>
#include <KernelExport.h>

namespace AppleG5 {

status_t mpic_init();
status_t mpic_init_per_cpu(int32 cpu);
int32 mpic_acknowledge();
void mpic_eoi();
void mpic_enable(int32 irq);
void mpic_disable(int32 irq);
void mpic_configure(int32 irq, bool level, bool activeHigh);
int32 mpic_assign_to_cpu(int32 irq, int32 cpu);
void mpic_send_ipi(int32 cpu, uint8 vector);
uint32 mpic_cpu_count();
bool mpic_is_initialized();

}

#endif
