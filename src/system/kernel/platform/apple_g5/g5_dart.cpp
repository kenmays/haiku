/*
 * Apple Power Mac G5 U3/U3H DART support.
 *
 * The DART is the system IOMMU used by the U3 northbridge.  Keep the
 * implementation deliberately small: the kernel's DMA layer can use this
 * interface without exposing chipset registers to individual drivers.
 */
#include "g5_dart.h"

#include <KernelExport.h>
#include <arch/cpu.h>
#include <boot/kernel_args.h>
#include <string.h>

namespace {

volatile uint32* sRegisters;
uint32* sTable;
size_t sTableBytes;
addr_t sBase;

static inline void
writeReg(uint32 offset, uint32 value)
{
	sRegisters[offset / sizeof(uint32)] = value;
	eieio();
}

static inline uint32
readReg(uint32 offset)
{
	uint32 value = sRegisters[offset / sizeof(uint32)];
	eieio();
	return value;
}

}

namespace AppleG5DART {

status_t
Init(addr_t base, size_t size)
{
	if (base == 0 || size < B_PAGE_SIZE)
		return B_BAD_VALUE;

	/*
	 * The exact register window is supplied by Open Firmware.  This routine
	 * intentionally receives the already-mapped kernel address rather than
	 * guessing a U3 physical address; G5 revisions place the DART differently.
	 */
	sRegisters = (volatile uint32*)base;
	sBase = base;

	/*
	 * DART page-table entries are 32-bit physical-page entries.  Allocate a
	 * 1 MiB table, sufficient for a 256K-entry, 4 KiB-page DMA aperture.
	 * The table is zeroed before enabling translation.
	 */
	sTableBytes = 1 << 20;
	sTable = (uint32*)kernel_memory_allocate(sTableBytes,
		B_PAGE_SIZE, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (sTable == NULL)
		return B_NO_MEMORY;
	memset(sTable, 0, sTableBytes);

	arch_cpu_sync_icache(sTable, sTableBytes);

	/* U3 DART registers: table base, control, and flush. */
	writeReg(0x00, (uint32)((addr_t)sTable >> 12));
	writeReg(0x0c, 0x00000001); // enable translation
	writeReg(0x08, 0xffffffff); // invalidate all cached translations

	return B_OK;
}

void
Shutdown()
{
	if (sRegisters != NULL)
		writeReg(0x0c, 0);
	if (sTable != NULL) {
		kernel_memory_free(sTable, sTableBytes);
		sTable = NULL;
	}
	sRegisters = NULL;
	sTableBytes = 0;
	sBase = 0;
}

status_t
Map(addr_t physical, size_t size, addr_t* _dmaAddress)
{
	if (sTable == NULL || _dmaAddress == NULL || size == 0)
		return B_BAD_VALUE;

	physical &= ~(addr_t)(B_PAGE_SIZE - 1);
	size = (size + B_PAGE_SIZE - 1) & ~(size_t)(B_PAGE_SIZE - 1);

	const size_t pageCount = size / B_PAGE_SIZE;
	const size_t first = (size_t)(physical / B_PAGE_SIZE);
	const size_t entries = sTableBytes / sizeof(uint32);
	if (first + pageCount > entries)
		return B_BAD_VALUE;

	for (size_t i = 0; i < pageCount; i++)
		sTable[first + i] = (uint32)((physical + i * B_PAGE_SIZE) >> 12) | 1;

	eieio();
	writeReg(0x08, 0xffffffff);
	*_dmaAddress = physical;
	return B_OK;
}

status_t
Unmap(addr_t dmaAddress, size_t size)
{
	if (sTable == NULL || size == 0)
		return B_BAD_VALUE;

	dmaAddress &= ~(addr_t)(B_PAGE_SIZE - 1);
	size = (size + B_PAGE_SIZE - 1) & ~(size_t)(B_PAGE_SIZE - 1);
	const size_t first = (size_t)(dmaAddress / B_PAGE_SIZE);
	const size_t pageCount = size / B_PAGE_SIZE;
	const size_t entries = sTableBytes / sizeof(uint32);
	if (first + pageCount > entries)
		return B_BAD_VALUE;

	memset(sTable + first, 0, pageCount * sizeof(uint32));
	eieio();
	writeReg(0x08, 0xffffffff);
	return B_OK;
}

addr_t Base() { return sBase; }
size_t Size() { return sTableBytes; }

}
