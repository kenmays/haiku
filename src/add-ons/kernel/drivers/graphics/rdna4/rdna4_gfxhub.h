#ifndef RDNA4_GFXHUB_H
#define RDNA4_GFXHUB_H

#include <SupportDefs.h>

struct rdna4_device;

/*
 * Native GFX12 VM hub support.
 *
 * These offsets are GFX12.0/12.0.1 GC register indices. MMIO addresses are
 * byte addressed, so callers multiply the register index by four.
 */
status_t rdna4_gfxhub_init(rdna4_device& device);
void rdna4_gfxhub_uninit(rdna4_device& device);
status_t rdna4_gfxhub_flush_tlb(rdna4_device& device, uint32 vmid,
	uint32 flushType);

#endif
