#ifndef RDNA4_MES_API_H
#define RDNA4_MES_API_H
#include <SupportDefs.h>
struct rdna4_device;
enum rdna4_mes_queue_type { RDNA4_MES_GFX=0, RDNA4_MES_COMPUTE=1, RDNA4_MES_SDMA=2 };
struct rdna4_mes_queue {
 uint32 id; uint32 doorbell; rdna4_mes_queue_type type; uint64 mqd; uint64 wptr; uint64 h_context; uint64 h_queue; bool active;
};
status_t rdna4_mes_set_hw_resources(rdna4_device&, uint32 vmidMaskMM, uint32 vmidMaskGFX, uint32 gfxMask, uint32 computeMask, uint32 sdmaMask);
status_t rdna4_mes_set_scheduling_config(rdna4_device&, uint64 processQuantum, uint64 gracePeriod, uint32 yieldPercent);
status_t rdna4_mes_add_queue(rdna4_device&, rdna4_mes_queue&, uint32 processId, uint64 pageTable, uint64 vaStart, uint64 vaEnd);
status_t rdna4_mes_remove_queue(rdna4_device&, rdna4_mes_queue&);
status_t rdna4_mes_wait_api(rdna4_device&, uint64 fence, bigtime_t timeout);
#endif
