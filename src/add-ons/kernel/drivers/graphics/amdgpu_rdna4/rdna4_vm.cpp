#include "rdna4_vm.h"

#include <stdlib.h>

static const uint64 kPageSize = 4096;
static const uint64 kVAWidth = 48;
static const uint64 kVAMask = (1ULL << kVAWidth) - 1;

static bool
range_overflows(uint64 address, uint64 size)
{
	return size > UINT64_MAX - address;
}

static bool
ranges_overlap(uint64 a, uint64 asize, uint64 b, uint64 bsize)
{
	return a < b + bsize && b < a + asize;
}

RDNA4VM::RDNA4VM()
	: fInitialized(false),
	  fMappings(NULL)
{
}

RDNA4VM::~RDNA4VM()
{
	Mapping* mapping = fMappings;
	while (mapping != NULL) {
		Mapping* next = mapping->next;
		free(mapping);
		mapping = next;
	}
	fMappings = NULL;
}

status_t
RDNA4VM::Initialize()
{
	fInitialized = true;
	return B_OK;
}

status_t
RDNA4VM::Map(uint64 gpuVA, uint64 physical, uint64 size, uint64 flags)
{
	if (!fInitialized)
		return B_NO_INIT;
	if (gpuVA == 0 || physical == 0 || size == 0)
		return B_BAD_VALUE;
	if ((gpuVA & (kPageSize - 1)) != 0
		|| (physical & (kPageSize - 1)) != 0
		|| (size & (kPageSize - 1)) != 0)
		return B_BAD_VALUE;
	if ((gpuVA & ~kVAMask) != 0 || range_overflows(gpuVA, size)
		|| gpuVA + size > (1ULL << kVAWidth))
		return B_BAD_VALUE;
	if (range_overflows(physical, size))
		return B_BAD_VALUE;

	for (Mapping* current = fMappings; current != NULL; current = current->next) {
		if (ranges_overlap(gpuVA, size, current->gpuVA, current->size))
			return B_BUSY;
	}

	Mapping* mapping = (Mapping*)malloc(sizeof(Mapping));
	if (mapping == NULL)
		return B_NO_MEMORY;

	mapping->gpuVA = gpuVA;
	mapping->physical = physical;
	mapping->size = size;
	mapping->flags = flags;
	mapping->next = fMappings;
	fMappings = mapping;

	return B_OK;
}

status_t
RDNA4VM::Unmap(uint64 gpuVA, uint64 size)
{
	if (!fInitialized)
		return B_NO_INIT;
	if (gpuVA == 0 || size == 0 || (gpuVA & (kPageSize - 1)) != 0
		|| (size & (kPageSize - 1)) != 0
		|| range_overflows(gpuVA, size)
		|| gpuVA + size > (1ULL << kVAWidth))
		return B_BAD_VALUE;

	Mapping** link = &fMappings;
	while (*link != NULL) {
		Mapping* mapping = *link;
		if (mapping->gpuVA == gpuVA && mapping->size == size) {
			*link = mapping->next;
			free(mapping);
			return B_OK;
		}
		link = &mapping->next;
	}

	return B_ENTRY_NOT_FOUND;
}

status_t
RDNA4VM::Flush()
{
	if (!fInitialized)
		return B_NO_INIT;

	/* The mapping list is the authoritative software state.  The eventual
	 * GFX12 implementation must follow this with the VM invalidate sequence
	 * for the affected VMID; there is deliberately no guessed MMIO here. */
	return B_OK;
}