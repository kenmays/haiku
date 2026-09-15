#ifndef RDNA4_MM_H
#define RDNA4_MM_H

#include <SupportDefs.h>

struct rdna4_bo {
	uint64 size;
	uint64 gpu_address;
	uint32 flags;
	area_id area;
	bool vram;
	bool scanout;
	bool pinned;
};

class RDNA4MemoryManager {
public:
	RDNA4MemoryManager();
	status_t Initialize(uint64 vramSize, uint64 gttSize);
	status_t Allocate(uint64 size, uint64 alignment, uint32 flags,
		rdna4_bo** _bo);
	status_t Free(rdna4_bo* bo);
	uint64 VRAMSize() const { return fVRAMSize; }
	uint64 GTTSize() const { return fGTTSize; }

private:
	uint64 fVRAMSize;
	uint64 fGTTSize;
	uint64 fNextGPUAddress;
};

#endif
