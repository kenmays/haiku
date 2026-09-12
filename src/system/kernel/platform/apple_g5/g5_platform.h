/*
 * Power Mac G5 platform support.
 *
 * The G5 family is based on the 64-bit PowerPC 970/970FX/970MP and
 * uses Apple's U3/U3H northbridge and K2 I/O controller family.
 */
#ifndef _KERNEL_PLATFORM_APPLE_G5_H
#define _KERNEL_PLATFORM_APPLE_G5_H

#include <SupportDefs.h>

namespace AppleG5 {

enum chipset_type {
	CHIPSET_UNKNOWN = 0,
	CHIPSET_U3,
	CHIPSET_U3H
};

enum cpu_type {
	CPU_UNKNOWN = 0,
	CPU_970,
	CPU_970FX,
	CPU_970MP
};

struct machine_info {
	chipset_type chipset;
	cpu_type cpu;
	uint32 cpuCount;
	uint64 memorySize;
};

bool detect(machine_info& info);

}

#endif
