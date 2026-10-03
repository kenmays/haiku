#ifndef RDNA4_DRIVER_H
#define RDNA4_DRIVER_H

#include <KernelExport.h>
#include <PCI.h>
#include <kernel/lock.h>

#include "rdna4.h"

struct rdna4_device {
	int32 id;
	pci_info* pci;
	uint32 device_id;
	uint32 gfx_ip;
	uint32 revision;

	addr_t mmio_phys;
	size_t mmio_size;
	uint8* mmio;
	area_id mmio_area;

	addr_t fb_phys;
	size_t fb_size;
	uint8* framebuffer;
	area_id framebuffer_area;

	area_id shared_area;
	rdna4_shared_info* shared;

	int32 open_count;
	status_t init_status;
	mutex lock;
};

extern pci_module_info* gPCI;
extern mutex gDriverLock;
extern rdna4_device* gDevices[RDNA4_MAX_CARDS];
extern char* gDeviceNames[RDNA4_MAX_CARDS + 1];
extern device_hooks gDeviceHooks;

status_t rdna4_init(rdna4_device& device);
void rdna4_uninit(rdna4_device& device);
status_t rdna4_ioctl(rdna4_device& device, uint32 op, void* buffer,
	size_t length);

#endif
