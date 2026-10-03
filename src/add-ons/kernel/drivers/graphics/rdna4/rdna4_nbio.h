#ifndef RDNA4_NBIO_H
#define RDNA4_NBIO_H

#include <SupportDefs.h>
struct rdna4_device;

status_t rdna4_nbio_init(rdna4_device& device);
void rdna4_nbio_uninit(rdna4_device& device);

#endif
