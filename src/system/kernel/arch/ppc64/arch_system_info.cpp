/* PPC64 system information for PPC970-family CPUs. */
#include <OS.h>
#include <arch_cpu.h>
#include <arch/system_info.h>
#include <boot/kernel_args.h>

enum cpu_vendor sCPUVendor = B_CPU_VENDOR_UNKNOWN;
uint32 sPVR = 0;
static uint64 sCPUClockFrequency;
static uint64 sBusClockFrequency;

void arch_fill_topology_node(cpu_topology_node_info* node, int32)
{
	switch (node->type) {
		case B_TOPOLOGY_ROOT:
			node->data.root.platform = B_CPU_PPC_64;
			break;
		case B_TOPOLOGY_PACKAGE:
			node->data.package.vendor = sCPUVendor;
			node->data.package.cache_line_size = CACHE_LINE_SIZE;
			break;
		case B_TOPOLOGY_CORE:
			node->data.core.model = sPVR;
			node->data.core.default_frequency = sCPUClockFrequency;
			break;
		default:
			break;
	}
}

status_t arch_system_info_init(kernel_args* args)
{
	sCPUClockFrequency = args->arch_args.cpu_frequency;
	sBusClockFrequency = args->arch_args.bus_frequency;
	sPVR = get_pvr();
	sCPUVendor = B_CPU_VENDOR_IBM;
	return B_OK;
}

status_t arch_get_frequency(uint64* frequency, int32)
{
	*frequency = sCPUClockFrequency;
	return B_OK;
}
