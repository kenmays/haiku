#ifndef _APPLE_G5_SMP_H
#define _APPLE_G5_SMP_H

#include <SupportDefs.h>
#include <boot/stage2.h>

namespace AppleG5 {

status_t smp_release_secondary(kernel_args* args, int32 cpu,
	addr_t entry, addr_t stack);

}

#endif
