/* PPC64 Open Firmware MMU bootstrap for PowerPC 970-class Power Macs. */
#include <OS.h>
#include <platform_arch.h>
#include <boot/addr_range.h>
#include <boot/kernel_args.h>
#include <boot/platform.h>
#include <boot/stage2.h>
#include <boot/stdio.h>
#include <platform/openfirmware/openfirmware.h>
#include <kernel.h>
#include <string.h>

#define PPC64_HPT_PTEG_SIZE 128
#define PPC64_HPT_MIN_SIZE (256 * 1024)
#define PPC64_HPT_MAX_SIZE (64 * 1024 * 1024)
#define PPC64_EXCEPTION_AREA_SIZE 0x2000

static size_t sHPTSize;
static void* sHPTAddress;

static size_t round_power_of_two(size_t value)
{
	size_t result = PPC64_HPT_MIN_SIZE;
	while (result < value && result < PPC64_HPT_MAX_SIZE)
		result <<= 1;
	return result;
}

static status_t find_physical_memory_ranges(size_t& total)
{
	int memory;
	if (of_getprop(gChosen, "memory", &memory, sizeof(memory)) == OF_FAILED)
		return B_ERROR;

	int package = of_instance_to_package(memory);
	int root = of_finddevice("/");
	int32 addressCells = of_address_cells(root);
	int32 sizeCells = of_size_cells(root);
	if (addressCells == OF_FAILED || sizeCells == OF_FAILED)
		return B_ERROR;
	if (addressCells > 2 || sizeCells > 1)
		return B_NOT_SUPPORTED;

	total = 0;
	if (addressCells == 2) {
		of_region<uint64, uint32> regions[64];
		int bytes = of_getprop(package, "reg", regions, sizeof(regions));
		if (bytes == OF_FAILED)
			bytes = of_getprop(memory, "reg", regions, sizeof(regions));
		if (bytes == OF_FAILED)
			return B_ERROR;
		int count = bytes / sizeof(regions[0]);
		for (int i = 0; i < count; i++) {
			if (regions[i].size == 0)
				continue;
			if (insert_physical_memory_range((addr_t)regions[i].base,
				regions[i].size) != B_OK)
				return B_ERROR;
			total += regions[i].size;
		}
		return B_OK;
	}

	of_region<uint32, uint32> regions[64];
	int bytes = of_getprop(package, "reg", regions, sizeof(regions));
	if (bytes == OF_FAILED)
		bytes = of_getprop(memory, "reg", regions, sizeof(regions));
	if (bytes == OF_FAILED)
		return B_ERROR;
	int count = bytes / sizeof(regions[0]);
	for (int i = 0; i < count; i++) {
		if (regions[i].size == 0)
			continue;
		if (insert_physical_memory_range((addr_t)regions[i].base,
			regions[i].size) != B_OK)
			return B_ERROR;
		total += regions[i].size;
	}
	return B_OK;
}

static status_t allocate_hpt(size_t totalMemory)
{
	size_t wanted = totalMemory / 64;
	if (wanted < PPC64_HPT_MIN_SIZE)
		wanted = PPC64_HPT_MIN_SIZE;
	if (wanted > PPC64_HPT_MAX_SIZE)
		wanted = PPC64_HPT_MAX_SIZE;

	sHPTSize = round_power_of_two(wanted);
	if (sHPTSize > PPC64_HPT_MAX_SIZE)
		sHPTSize = PPC64_HPT_MAX_SIZE;

	sHPTAddress = of_claim(NULL, sHPTSize, sHPTSize);
	if (sHPTAddress == NULL || sHPTAddress == (void*)OF_FAILED)
		return B_NO_MEMORY;

	memset(sHPTAddress, 0, sHPTSize);
	insert_physical_allocated_range((addr_t)sHPTAddress, sHPTSize);
	insert_virtual_allocated_range((addr_t)sHPTAddress, sHPTSize);

	gKernelArgs.arch_args.page_table.start = (addr_t)sHPTAddress;
	gKernelArgs.arch_args.page_table.size = sHPTSize;

	dprintf("PPC64: HPT at %p, size %" B_PRIuSIZE " bytes (%" B_PRIuSIZE
		" PTEGs)\n", sHPTAddress, sHPTSize,
		sHPTSize / PPC64_HPT_PTEG_SIZE);
	return B_OK;
}

extern "C" void* arch_mmu_allocate(void* virtualAddress, size_t size,
	uint8 protection, bool exactAddress)
{
	(void)protection;
	(void)exactAddress;
	size = ROUNDUP(size, B_PAGE_SIZE);
	if (size == 0)
		return NULL;
	if (virtualAddress != NULL)
		return virtualAddress;
	void* address = of_claim(NULL, size, B_PAGE_SIZE);
	return address == (void*)OF_FAILED ? NULL : address;
}

extern "C" status_t arch_mmu_free(void*, size_t)
{
	return B_OK;
}

extern "C" status_t arch_mmu_init(void)
{
	size_t total = 0;
	status_t error = find_physical_memory_ranges(total);
	if (error != B_OK)
		return error;

	dprintf("PPC64 OF memory: %" B_PRIuSIZE " MB\n", total / (1024 * 1024));
	error = allocate_hpt(total);
	if (error != B_OK)
		return error;

	/* The PPC970 takes all real-mode exception vectors from physical 0.
	 * Reserve enough space for every vector through 0x1700. */
	gKernelArgs.arch_args.exception_handlers.start = 0;
	gKernelArgs.arch_args.exception_handlers.size = PPC64_EXCEPTION_AREA_SIZE;
	return B_OK;
}
