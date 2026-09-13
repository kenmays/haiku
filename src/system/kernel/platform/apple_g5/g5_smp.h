#ifndef _APPLE_G5_SMP_H
#define _APPLE_G5_SMP_H

#include <SupportDefs.h>
#include <boot/stage2.h>

namespace AppleG5 {

status_t smp_release_secondary(kernel_args* args, int32 cpu,
	addr_t entry, addr_t stack);

/* Start an OF-stopped CPU at a real-mode trampoline. physicalArgument is
 * passed to the trampoline in PPC64 r3. */
status_t smp_start_cpu_of(int32 cpu, addr_t realModePC,
	addr_t physicalArgument);

}

#endif
