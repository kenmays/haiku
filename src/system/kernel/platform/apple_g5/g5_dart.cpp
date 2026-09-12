/* Apple Power Mac G5 U3/U3H DART support. */
#include "g5_dart.h"

#include <KernelExport.h>
#include <ByteOrder.h>
#include <arch/cpu.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_types.h>
#include <string.h>

namespace {
static volatile uint32* sRegisters;
static uint32* sTable;
static phys_addr_t sTablePhysical;
static uint8* sUsed;
static size_t sTableBytes;
static size_t sEntries;
static bool sEnabled;

static const uint32 DART_CNTL = 0x00;
static const uint32 DART_CNTL_BASE_SHIFT = 12;
static const uint32 DART_CNTL_BASE_MASK = 0xfffff;
static const uint32 DART_CNTL_FLUSHTLB = 0x400;
static const uint32 DART_CNTL_ENABLE = 0x200;
static const uint32 DART_CNTL_SIZE_MASK = 0x1ff;
static const uint32 DARTMAP_VALID = 0x80000000U;
static const uint32 DARTMAP_RPNMASK = 0x00ffffffU;

static inline uint32 readReg(uint32 offset)
{
	return B_BENDIAN_TO_HOST_INT32(sRegisters[offset / 4]);
}
static inline void writeReg(uint32 offset, uint32 value)
{
	sRegisters[offset / 4] = B_HOST_TO_BENDIAN_INT32(value);
	eieio();
}
static void flushTLB()
{
	uint32 value = readReg(DART_CNTL);
	writeReg(DART_CNTL, value | DART_CNTL_FLUSHTLB);
	for (uint32 i = 0; i < 100000; i++) {
		if ((readReg(DART_CNTL) & DART_CNTL_FLUSHTLB) == 0)
			return;
		asm volatile("sync" ::: "memory");
	}
	writeReg(DART_CNTL, value & ~DART_CNTL_FLUSHTLB);
	writeReg(DART_CNTL, value | DART_CNTL_FLUSHTLB);
}
static bool findRun(size_t pages, size_t& first)
{
	if (pages == 0 || pages >= sEntries - 1)
		return false;
	for (size_t p = 1; p + pages < sEntries; p++) {
		size_t i = 0;
		for (; i < pages; i++)
			if (sUsed[p + i]) break;
		if (i == pages) { first = p; return true; }
		p += i;
	}
	return false;
}
}

namespace AppleG5DART {

status_t Init(addr_t base, size_t size)
{
	if (base == 0 || size < 0x100)
		return B_BAD_VALUE;
	if (sEnabled)
		return B_OK;

	sRegisters = (volatile uint32*)base;
	sTableBytes = 2 * 1024 * 1024;
	sEntries = sTableBytes / sizeof(uint32);

	physical_address_restrictions restrictions = {};
	restrictions.low_address = 0;
	restrictions.high_address = 0x80000000ULL;
	restrictions.alignment = 16 * 1024 * 1024;
	vm_page* run = vm_page_allocate_page_run(PAGE_STATE_WIRED | VM_PAGE_ALLOC_CLEAR,
		sTableBytes / B_PAGE_SIZE, &restrictions, VM_PRIORITY_SYSTEM);
	if (run == NULL)
		return B_NO_MEMORY;
	sTablePhysical = run->physical_page_number * B_PAGE_SIZE;

	void* mapped = NULL;
	area_id area = map_physical_memory("g5-dart-table", sTablePhysical,
		sTableBytes, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &mapped);
	if (area < 0)
		return area;
	sTable = (uint32*)mapped;
	sUsed = (uint8*)kernel_memory_allocate(sEntries, B_PAGE_SIZE,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (sUsed == NULL)
		return B_NO_MEMORY;
	memset(sUsed, 0, sEntries);

	vm_page* dummy = vm_page_allocate_page_run(PAGE_STATE_WIRED | VM_PAGE_ALLOC_CLEAR,
		1, &restrictions, VM_PRIORITY_SYSTEM);
	if (dummy == NULL)
		return B_NO_MEMORY;
	const uint32 empty = DARTMAP_VALID
		| ((uint32)dummy->physical_page_number & DARTMAP_RPNMASK);
	for (size_t i = 0; i < sEntries; i++)
		sTable[i] = B_HOST_TO_BENDIAN_INT32(empty);
	sUsed[0] = 1;
	sUsed[sEntries - 1] = 1;
	arch_cpu_memory_write_barrier();

	const uint32 basePages = (uint32)(sTablePhysical >> 12);
	const uint32 tablePages = (uint32)(sTableBytes >> 12);
	writeReg(DART_CNTL, DART_CNTL_ENABLE
		| ((basePages & DART_CNTL_BASE_MASK) << DART_CNTL_BASE_SHIFT)
		| (tablePages & DART_CNTL_SIZE_MASK));
	flushTLB();
	sEnabled = true;
	dprintf("apple_g5: DART phys=%" B_PRIxPHYSADDR " entries=%" B_PRIuSIZE "\n",
		sTablePhysical, sEntries);
	return B_OK;
}

void Shutdown()
{
	if (sRegisters != NULL)
		writeReg(DART_CNTL, readReg(DART_CNTL) & ~DART_CNTL_ENABLE);
	sEnabled = false;
}

status_t Map(addr_t physical, size_t size, addr_t* _dmaAddress)
{
	if (!sEnabled || _dmaAddress == NULL || size == 0)
		return B_BAD_VALUE;
	physical &= ~(addr_t)(B_PAGE_SIZE - 1);
	size = (size + B_PAGE_SIZE - 1) & ~(size_t)(B_PAGE_SIZE - 1);
	const size_t pages = size / B_PAGE_SIZE;
	size_t first;
	if (!findRun(pages, first))
		return B_NO_MEMORY;
	for (size_t i = 0; i < pages; i++) {
		sTable[first + i] = B_HOST_TO_BENDIAN_INT32(
			DARTMAP_VALID | ((uint32)(physical >> 12) & DARTMAP_RPNMASK));
		sUsed[first + i] = 1;
		physical += B_PAGE_SIZE;
	}
	arch_cpu_memory_write_barrier();
	flushTLB();
	*_dmaAddress = first * B_PAGE_SIZE;
	return B_OK;
}

status_t Unmap(addr_t dmaAddress, size_t size)
{
	if (!sEnabled || size == 0)
		return B_BAD_VALUE;
	dmaAddress &= ~(addr_t)(B_PAGE_SIZE - 1);
	size = (size + B_PAGE_SIZE - 1) & ~(size_t)(B_PAGE_SIZE - 1);
	const size_t first = dmaAddress / B_PAGE_SIZE;
	const size_t pages = size / B_PAGE_SIZE;
	if (first == 0 || first + pages >= sEntries)
		return B_BAD_VALUE;
	for (size_t i = 0; i < pages; i++) {
		sTable[first + i] = 0;
		sUsed[first + i] = 0;
	}
	arch_cpu_memory_write_barrier();
	flushTLB();
	return B_OK;
}

addr_t Base() { return 0; }
size_t Size() { return sTableBytes; }

}
