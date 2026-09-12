/* Apple Power Mac G5 platform discovery and bootstrap. */
#include "g5_platform.h"
#include "g5_dart.h"
#include <arch_cpu.h>
#include <platform/openfirmware/openfirmware.h>
#include <boot/kernel_args.h>
#include <debug.h>
#include <vm/vm.h>
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

static bool find_compatible(const char* type, const char* compatible,
	intptr_t& _node)
{
	intptr_t cookie = 0;
	while (true) {
		char path[B_PATH_NAME_LENGTH];
		if (of_get_next_device(&cookie, 0, type, path, sizeof(path)) != B_OK)
			break;
		char buffer[256];
		int length = of_getprop(cookie, "compatible", buffer,
			sizeof(buffer) - 1);
		if (length <= 0)
			continue;
		buffer[length] = '\0';
		if (strstr(buffer, compatible) != NULL) {
			_node = cookie;
			return true;
		}
	}
	return false;
}

static bool get_reg(intptr_t node, phys_addr_t& _base, size_t& _size)
{
	intptr_t parent = of_parent(node);
	uint32 addressCells = 2;
	uint32 sizeCells = 1;
	if (parent > 0) {
		uint32 value;
		if (of_getprop(parent, "#address-cells", &value, sizeof(value)) > 0)
			addressCells = B_BENDIAN_TO_HOST_INT32(value);
		if (of_getprop(parent, "#size-cells", &value, sizeof(value)) > 0)
			sizeCells = B_BENDIAN_TO_HOST_INT32(value);
	}

	uint32 reg[8] = {};
	int length = of_getprop(node, "reg", reg, sizeof(reg));
	if (length <= 0 || addressCells == 0 || addressCells > 2
		|| sizeCells == 0 || sizeCells > 2)
		return false;
	const uint32* p = reg;
	phys_addr_t base = 0;
	for (uint32 i = 0; i < addressCells; i++)
		base = (base << 32) | B_BENDIAN_TO_HOST_INT32(*p++);
	uint64 size = 0;
	for (uint32 i = 0; i < sizeCells; i++)
		size = (size << 32) | B_BENDIAN_TO_HOST_INT32(*p++);
	if (base == 0 || size == 0)
		return false;
	_base = base;
	_size = (size_t)size;
	return true;
}

bool detect(machine_info& info, kernel_args* args)
{
	info.chipset = CHIPSET_UNKNOWN;
	info.cpu = CPU_UNKNOWN;
	info.cpuCount = args != NULL ? args->num_cpus : 1;
	info.memorySize = 0;
	if (args != NULL) {
		for (uint32 i = 0; i < args->num_physical_memory_ranges; i++)
			info.memorySize += args->physical_memory_range[i].size;
	}
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

status_t init(kernel_args* args)
{
	machine_info info;
	if (!detect(info, args))
		return B_BAD_VALUE;
	dprintf("Apple G5: CPU=%d chipset=%d CPUs=%" B_PRIu32
		" memory=%" B_PRIu64 " MB\n", info.cpu, info.chipset,
		info.cpuCount, info.memorySize / (1024 * 1024));
	return B_OK;
}

status_t init_post_vm(kernel_args*)
{
	intptr_t node = 0;
	if (!find_compatible("dart", "u3-dart", node)
		&& !find_compatible("dart", "u4-dart", node)) {
		dprintf("apple_g5: no DART node in Open Firmware\n");
		return B_OK;
	}

	phys_addr_t physical;
	size_t size;
	if (!get_reg(node, physical, size)) {
		dprintf("apple_g5: invalid DART reg property\n");
		return B_BAD_VALUE;
	}

	void* mapped = NULL;
	area_id area = map_physical_memory("g5-dart-registers", physical, size,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &mapped);
	if (area < 0)
		return area;

	status_t status = AppleG5DART::Init((addr_t)mapped, size);
	if (status != B_OK)
		dprintf("apple_g5: DART initialization failed: %" B_PRId32 "\n", status);
	return status;
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
