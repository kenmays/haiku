#ifndef RDNA4_PSP_H
#define RDNA4_PSP_H

#include <SupportDefs.h>

struct rdna4_device;

status_t rdna4_psp_init(rdna4_device& device);
void rdna4_psp_uninit(rdna4_device& device);
status_t rdna4_psp_ring_create(rdna4_device& device);
void rdna4_psp_ring_destroy(rdna4_device& device);
status_t rdna4_psp_load_firmware(rdna4_device& device);
status_t rdna4_psp_mode1_reset(rdna4_device& device);
status_t rdna4_psp_load_ip_firmware(rdna4_device& device, uint32 type,
	uint32 pspType);

#endif
