#ifndef RDNA4_VM_H
#define RDNA4_VM_H

#include <SupportDefs.h>

/* GFX12 uses 48-bit virtual addresses, 4 KiB pages and four page-table
 * levels.  This class owns the software representation; hardware table
 * programming is enabled by the kernel driver once its MMIO context is
 * available. */
class RDNA4VM {
public:
	RDNA4VM();
	~RDNA4VM();

	status_t Initialize();
	status_t Map(uint64 gpuVA, uint64 physical, uint64 size, uint64 flags);
	status_t Unmap(uint64 gpuVA, uint64 size);
	status_t Flush();

private:
	struct Mapping {
		uint64 gpuVA;
		uint64 physical;
		uint64 size;
		uint64 flags;
		Mapping* next;
	};

	bool fInitialized;
	Mapping* fMappings;
};

#endif
