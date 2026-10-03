#ifndef RDNA4_DRIVER_H
#define RDNA4_DRIVER_H

#include <KernelExport.h>
#include <PCI.h>
#include <kernel/lock.h>

#include "rdna4.h"
#include "rdna4_bo.h"

#define RDNA4_VM_MAX_TABLES 1024
#define RDNA4_VM_MAX_BOS 256

struct rdna4_vm_table {
	area_id area;
	uint64* cpu;
	phys_addr_t phys;
	uint8 level;
	uint8 used;
	uint16 reserved;
	uint64 base;
};

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

	bool vm_ready;
	bool gfxhub_ready;
	uint32 vm_root_index;
	phys_addr_t vm_root_phys;
	uint64 vm_next_va;

	area_id gfx_ring_area;
	void* gfx_ring_cpu;
	area_id gfx_ring_rptr_area;
	volatile uint32* gfx_ring_rptr_cpu;
	phys_addr_t gfx_ring_rptr_phys;
	uint64 gfx_ring_rptr_gpu;
	volatile uint32* gfx_ring_wptr_poll_cpu;
	uint64 gfx_ring_wptr_poll_gpu;
	phys_addr_t gfx_ring_phys;
	uint64 gfx_ring_gpu;
	uint32 gfx_ring_dwords;
	uint32 gfx_ring_wptr;
	uint32 gfx_ring_rptr;
	bool gfx_ring_ready;
	rdna4_bo gfx_ring_bo;
	rdna4_vm_table vm_tables[RDNA4_VM_MAX_TABLES];
	rdna4_bo bos[RDNA4_VM_MAX_BOS];
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
