/* Apple Power Mac G5 U3/U3H/U4 DART support. */
#include "g5_dart.h"
#include <KernelExport.h>
#include <ByteOrder.h>
#include <arch/cpu.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_types.h>
#include <lock.h>
#include <string.h>

namespace {
static volatile uint32* sRegisters;
static uint32* sTable;
static phys_addr_t sTablePhysical;
static uint8* sUsed;
static size_t sTableBytes;
static size_t sEntries;
static bool sEnabled;
static bool sU4;
static spinlock sLock = B_SPINLOCK_INITIALIZER;
static area_id sTableArea = -1;

static const uint32 U3_BASE_SHIFT = 12;
static const uint32 U3_BASE_MASK = 0xfffff;
static const uint32 U3_FLUSH = 0x400;
static const uint32 U3_ENABLE = 0x200;
static const uint32 U3_SIZE_MASK = 0x1ff;
static const uint32 U4_BASE = 0x10;
static const uint32 U4_SIZE = 0x20;
static const uint32 U4_FLUSH = 0x20000000U;
static const uint32 U4_ENABLE = 0x80000000U;
static const uint32 U4_SIZE_MASK = 0x1fff;
static const uint32 VALID = 0x80000000U;
static const uint32 RPNMASK = 0x00ffffffU;

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
	uint32 value = readReg(0);
	const uint32 flush = sU4 ? U4_FLUSH : U3_FLUSH;
	writeReg(0, value | flush);
	for (uint32 i = 0; i < 100000; i++) {
		if ((readReg(0) & flush) == 0)
			return;
		asm volatile("sync" ::: "memory");
	}
}

static bool findRun(size_t pages, size_t& first)
{
	if (pages == 0 || pages >= sEntries - 1)
		return false;
	for (size_t p = 1; p + pages < sEntries; p++) {
		size_t freePages = 0;
		while (freePages < pages && !sUsed[p + freePages])
			freePages++;
		if (freePages == pages) {
			first = p;
			return true;
		}
		p += freePages;
	}
	return false;
}
}

namespace AppleG5DART {

status_t
Init(addr_t base, size_t size, bool u4)
{
	if (base == 0 || size < 0x100)
		return B_BAD_VALUE;
	if (sEnabled)
		return B_OK;
	sRegisters = (volatile uint32*)base;
	sU4 = u4;
	sTableBytes = 2 * 1024 * 1024;
	sEntries = sTableBytes / 4;

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
	sTableArea = map_physical_memory("g5-dart-table", sTablePhysical,
		sTableBytes, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &mapped);
	if (sTableArea < 0)
		return sTableArea;
	sTable = (uint32*)mapped;

	sUsed = (uint8*)kernel_memory_allocate(sEntries, B_PAGE_SIZE,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (sUsed == NULL)
		return B_NO_MEMORY;
	memset(sUsed, 0, sEntries);

	physical_address_restrictions dummyRestrictions = {};
	dummyRestrictions.low_address = 0;
	dummyRestrictions.high_address = 0x80000000ULL;
	vm_page* dummy = vm_page_allocate_page_run(PAGE_STATE_WIRED | VM_PAGE_ALLOC_CLEAR,
		1, &dummyRestrictions, VM_PRIORITY_SYSTEM);
	if (dummy == NULL)
		return B_NO_MEMORY;

	const uint32 empty = VALID | ((uint32)dummy->physical_page_number & RPNMASK);
	for (size_t i = 0; i < sEntries; i++)
		sTable[i] = B_HOST_TO_BENDIAN_INT32(empty);
	sUsed[0] = sUsed[sEntries - 1] = 1;
	arch_cpu_memory_write_barrier();

	const uint32 basePages = (uint32)(sTablePhysical >> 12);
	const uint32 tablePages = (uint32)(sTableBytes >> 12);
	if (sU4) {
		writeReg(U4_BASE, basePages);
		writeReg(U4_SIZE, tablePages & U4_SIZE_MASK);
		writeReg(0, U4_ENABLE);
	} else {
		writeReg(0, U3_ENABLE
			| ((basePages & U3_BASE_MASK) << U3_BASE_SHIFT)
			| (tablePages & U3_SIZE_MASK));
	}
	flushTLB();
	sEnabled = true;
	dprintf("apple_g5: DART %s phys=%" B_PRIxPHYSADDR " entries=%" B_PRIuSIZE "\n",
		sU4 ? "U4" : "U3", sTablePhysical, sEntries);
	return B_OK;
}

void
Shutdown()
{
	if (sRegisters != NULL)
		writeReg(0, readReg(0) & ~(sU4 ? U4_ENABLE : U3_ENABLE));
	sEnabled = false;
}

status_t
Map(addr_t physical, size_t size, addr_t* out)
{
	if (!sEnabled || out == NULL || size == 0)
		return B_BAD_VALUE;
	InterruptsSpinLocker locker(sLock);

	const addr_t offset = physical & (B_PAGE_SIZE - 1);
	const addr_t pagePhysical = physical - offset;
	const size_t mappedSize = (size + offset + B_PAGE_SIZE - 1)
		& ~(size_t)(B_PAGE_SIZE - 1);
	const size_t pages = mappedSize / B_PAGE_SIZE;
	size_t first;
	if (!findRun(pages, first))
		return B_NO_MEMORY;

	for (size_t i = 0; i < pages; i++) {
		const addr_t page = pagePhysical + i * B_PAGE_SIZE;
		if ((page >> 12) > RPNMASK) {
			for (size_t j = 0; j < i; j++) { sTable[first + j] = 0; sUsed[first + j] = 0; }
			return B_BAD_VALUE;
		}
		sTable[first + i] = B_HOST_TO_BENDIAN_INT32(
			VALID | ((uint32)(page >> 12) & RPNMASK));
		sUsed[first + i] = 1;
	}
	arch_cpu_memory_write_barrier();
	flushTLB();
	*out = first * B_PAGE_SIZE + offset;
	return B_OK;
}

status_t
Unmap(addr_t dma, size_t size)
{
	if (!sEnabled || size == 0)
		return B_BAD_VALUE;
	InterruptsSpinLocker locker(sLock);
	const addr_t offset = dma & (B_PAGE_SIZE - 1);
	const addr_t pageDMA = dma - offset;
	const size_t mappedSize = (size + offset + B_PAGE_SIZE - 1)
		& ~(size_t)(B_PAGE_SIZE - 1);
	const size_t first = pageDMA / B_PAGE_SIZE;
	const size_t pages = mappedSize / B_PAGE_SIZE;
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

bool Enabled() { return sEnabled; }
addr_t Base() { return 0; }
size_t Size() { return sTableBytes; }

}
