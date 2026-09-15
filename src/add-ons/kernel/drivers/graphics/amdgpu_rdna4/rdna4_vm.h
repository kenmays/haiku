#ifndef RDNA4_VM_H
#define RDNA4_VM_H

#include <SupportDefs.h>

class RDNA4VM {
public:
	RDNA4VM();
	status_t Initialize();
	status_t Map(uint64 gpuVA, uint64 physical, uint64 size, uint64 flags);
	status_t Unmap(uint64 gpuVA, uint64 size);
	status_t Flush();

private:
	bool fInitialized;
};

#endif
