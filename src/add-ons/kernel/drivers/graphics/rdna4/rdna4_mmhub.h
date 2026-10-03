#ifndef RDNA4_MMHUB_H
#define RDNA4_MMHUB_H
#include <SupportDefs.h>
struct rdna4_device;
status_t rdna4_mmhub_init(rdna4_device&);
void rdna4_mmhub_uninit(rdna4_device&);
status_t rdna4_mmhub_flush(rdna4_device&,uint32 vmid);
#endif
