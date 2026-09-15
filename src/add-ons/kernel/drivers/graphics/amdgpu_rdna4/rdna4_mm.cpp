#include "rdna4_mm.h"

#include <stdlib.h>

RDNA4MemoryManager::RDNA4MemoryManager()
	: fVRAMSize(0), fGTTSize(0), fNextGPUAddress(0x100000000ULL)
{
}

status_t
RDNA4MemoryManager::Initialize(uint64 vramSize, uint64 gttSize)
{
	fVRAMSize = vramSize;
	fGTTSize = gttSize;
	fNextGPUAddress = 0x100000000ULL;
	return B_OK;
}

status_t
RDNA4MemoryManager::Allocate(uint64 size, uint64 alignment, uint32 flags,
	rdna4_bo** _bo)
{
	if (_bo == NULL || size == 0)
		return B_BAD_VALUE;
	if (alignment == 0)
		alignment = B_PAGE_SIZE;

	rdna4_bo* bo = (rdna4_bo*)calloc(1, sizeof(rdna4_bo));
	if (bo == NULL)
		return B_NO_MEMORY;

	uint64 mask = alignment - 1;
	fNextGPUAddress = (fNextGPUAddress + mask) & ~mask;

	bo->size = size;
	bo->gpu_address = fNextGPUAddress;
	bo->flags = flags;
	bo->area = -1;
	bo->vram = true;
	bo->scanout = false;
	bo->pinned = false;

	fNextGPUAddress += size;
	*_bo = bo;
	return B_OK;
}

status_t
RDNA4MemoryManager::Free(rdna4_bo* bo)
{
	free(bo);
	return B_OK;
}
