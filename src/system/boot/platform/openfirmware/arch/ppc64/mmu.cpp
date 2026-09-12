/* PPC64 Open Firmware MMU bootstrap.
 *
 * PPC970 enters through an existing Open Firmware translation environment.
 * This first implementation deliberately preserves that environment instead
 * of trying to convert the 32-bit PPC hash-table code to Book III-S in the
 * boot loader. The kernel takes ownership of the hash MMU after handoff.
 */
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
			if (regions[i].size == 0) continue;
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
		if (regions[i].size == 0) continue;
		if (insert_physical_memory_range((addr_t)regions[i].base,
			regions[i].size) != B_OK)
			return B_ERROR;
		total += regions[i].size;
	}
	return B_OK;
}

extern "C" void* arch_mmu_allocate(void* virtualAddress, size_t size,
	uint8 protection, bool exactAddress)
{
	(void)protection;
	size = ROUNDUP(size, B_PAGE_SIZE);
	if (size == 0) return NULL;

	/* Keep early allocations in the existing OF address space. */
	if (virtualAddress != NULL)
		return virtualAddress;

	void* address = of_claim(NULL, size, B_PAGE_SIZE);
	if (address == (void*)OF_FAILED || address == NULL)
		return NULL;
	return address;
}

extern "C" status_t arch_mmu_free(void*, size_t)
{
	/* Firmware owns the initial mappings until the kernel MMU takes over. */
	return B_OK;
}

extern "C" status_t arch_mmu_init(void)
{
	size_t total = 0;
	status_t error = find_physical_memory_ranges(total);
	if (error != B_OK)
		return error;

	dprintf("PPC64 OF memory: %" B_PRIuSIZE " MB\n", total / (1024 * 1024));

	/* Preserve the firmware translation environment. The kernel receives the
	 * firmware SDR1/SLB state and replaces it during VM initialization. */
	gKernelArgs.arch_args.exception_handlers.start = 0;
	gKernelArgs.arch_args.exception_handlers.size = B_PAGE_SIZE;
	return B_OK;
}
