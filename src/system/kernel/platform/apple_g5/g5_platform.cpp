/*
 * Power Mac G5 platform identification.
 *
 * Hardware initialization is deliberately kept out of this first layer.
 * The Open Firmware device tree is authoritative for U3/U3H/K2 resources;
 * fixed physical addresses must not be guessed from a particular G5 model.
 */
#include "g5_platform.h"

#include <arch_cpu.h>

namespace AppleG5 {

static cpu_type
cpu_from_pvr(uint32 pvr)
{
	uint16 version = (uint16)(pvr >> 16);

	/* IBM PPC970 family PVR versions used by Apple G5 systems. */
	switch (version) {
		case 0x0039: /* PPC970 */
			return CPU_970;
		case 0x003c: /* PPC970FX */
		case 0x0044:
			return CPU_970FX;
		case 0x0045: /* PPC970MP */
			return CPU_970MP;
		default:
			return CPU_UNKNOWN;
	}
}

bool
detect(machine_info& info)
{
	info.chipset = CHIPSET_UNKNOWN;
	info.cpu = CPU_UNKNOWN;
	info.cpuCount = 0;
	info.memorySize = 0;

#if defined(__powerpc64__)
	info.cpu = cpu_from_pvr(get_pvr());
	return info.cpu != CPU_UNKNOWN;
#else
	return false;
#endif
}

} // namespace AppleG5
