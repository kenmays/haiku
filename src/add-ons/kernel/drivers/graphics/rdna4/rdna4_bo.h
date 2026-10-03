#ifndef RDNA4_BO_H
#define RDNA4_BO_H

#include <SupportDefs.h>
#include <OS.h>

struct rdna4_bo {
	area_id area;
	void* cpu;
	uint64 size;
	uint64 alignment;
	phys_addr_t physical;
	uint64 gpu;
	uint32 flags;
	bool used;
};

#endif
