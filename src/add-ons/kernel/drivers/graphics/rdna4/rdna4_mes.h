#ifndef RDNA4_MES_H
#define RDNA4_MES_H
#include <SupportDefs.h>
struct rdna4_device;
status_t rdna4_mes_init(rdna4_device& device);
void rdna4_mes_uninit(rdna4_device& device);
status_t rdna4_mes_start(rdna4_device& device);
status_t rdna4_mes_stop(rdna4_device& device);
status_t rdna4_mes_wait(rdna4_device& device, bigtime_t timeout);
#endif
