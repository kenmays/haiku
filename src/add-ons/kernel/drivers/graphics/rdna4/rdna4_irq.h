#ifndef RDNA4_IRQ_H
#define RDNA4_IRQ_H
#include <SupportDefs.h>
struct rdna4_device;
status_t rdna4_irq_init(rdna4_device& device);
void rdna4_irq_uninit(rdna4_device& device);
status_t rdna4_wait_fence_event(rdna4_device& device, bigtime_t timeout);
#endif
