#ifndef RDNA4_SDMA_H
#define RDNA4_SDMA_H
#include <SupportDefs.h>
struct rdna4_device;
status_t rdna4_sdma_init(rdna4_device& device);
void rdna4_sdma_uninit(rdna4_device& device);
status_t rdna4_sdma_start(rdna4_device& device);
status_t rdna4_sdma_stop(rdna4_device& device);
status_t rdna4_sdma_wait_idle(rdna4_device& device, bigtime_t timeout);
status_t rdna4_sdma_submit_copy(rdna4_device& device, uint32 instance,
	uint64 src, uint64 dst, uint32 bytes, uint64 fence, uint64 fenceValue);
#endif
