#ifndef RDNA4_SDMA_H
#define RDNA4_SDMA_H

#include <SupportDefs.h>

#include "rdna4_ring.h"

class RDNA4SDMA {
public:
	RDNA4SDMA();
	status_t Initialize();
	status_t Copy(uint64 dst, uint64 src, uint64 size, uint64* _fence);
	status_t Fill(uint64 dst, uint64 size, uint32 value, uint64* _fence);

private:
	bool fInitialized;
	uint64 fSequence;
	RDNA4Ring fRing;
};

#endif
