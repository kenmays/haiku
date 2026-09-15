#ifndef RDNA4_GFX_H
#define RDNA4_GFX_H

#include <SupportDefs.h>

#include "rdna4_ring.h"

struct rdna4_fence {
	uint64 sequence;
};

class RDNA4GFX {
public:
	RDNA4GFX();
	status_t Initialize();
	status_t Submit(const uint32* commands, uint32 count,
		rdna4_fence* _fence);
	status_t Wait(const rdna4_fence& fence, bigtime_t timeout);
	status_t Reset();

private:
	bool fInitialized;
	uint64 fSequence;
	RDNA4Ring fRing;
};

#endif
