/* Apple Power Mac G5 platform discovery and bootstrap. */
#include "g5_platform.h"
#include <arch_cpu.h>
#include <platform/openfirmware/openfirmware.h>
#include <boot/kernel_args.h>
#include <debug.h>
#include <string.h>

namespace AppleG5 {

static cpu_type cpu_from_pvr(uint32 pvr)
{
	switch ((uint16)(pvr >> 16)) {
		case 0x0039: return CPU_970;
		case 0x003c:
		case 0x0044: return CPU_970FX;
		case 0x0045: return CPU_970MP;
		default: return CPU_UNKNOWN;
	}
}

static chipset_type chipset_from_model(const char* model)
{
	if (model == NULL)
		return CHIPSET_UNKNOWN;
	if (strstr(model, "U4") != NULL || strstr(model, "PowerMac11") != NULL)
		return CHIPSET_U3H;
	if (strstr(model, "U3H") != NULL)
		return CHIPSET_U3H;
	if (strstr(model, "U3") != NULL || strstr(model, "PowerMac") != NULL)
		return CHIPSET_U3;
	return CHIPSET_UNKNOWN;
}

bool detect(machine_info& info)
{
	info.chipset = CHIPSET_UNKNOWN;
	info.cpu = CPU_UNKNOWN;
	info.cpuCount = gKernelArgs.num_cpus;
	info.memorySize = total_physical_memory();
#if defined(__powerpc64__)
	info.cpu = cpu_from_pvr(get_pvr());
	char model[128] = {};
	if (of_getprop(gChosen, "model", model, sizeof(model)) != OF_FAILED)
		info.chipset = chipset_from_model(model);
	return info.cpu != CPU_UNKNOWN;
#else
	return false;
#endif
}

status_t init(kernel_args*)
{
	machine_info info;
	if (!detect(info))
		return B_BAD_VALUE;
	dprintf("Apple G5: CPU=%d chipset=%d CPUs=%" B_PRIu32
		" memory=%" B_PRIu64 " MB\n", info.cpu, info.chipset,
		info.cpuCount, info.memorySize / (1024 * 1024));
	return B_OK;
}

status_t init_post_vm(kernel_args*)
{
	return B_OK;
}

} // namespace AppleG5

extern "C" status_t apple_g5_platform_init(kernel_args* args)
{
	return AppleG5::init(args);
}
extern "C" status_t apple_g5_platform_init_post_vm(kernel_args* args)
{
	return AppleG5::init_post_vm(args);
}
