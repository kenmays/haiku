#ifndef RDNA4_DRIVER_H
#define RDNA4_DRIVER_H

#include <KernelExport.h>
#include <PCI.h>
#include <kernel/lock.h>

#include "rdna4.h"
#include "rdna4_bo.h"
#include "rdna4_fw.h"
#include "rdna4_discovery.h"

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

struct rdna4_firmware_slot {
	area_id area;
	void* cpu;
	phys_addr_t phys;
	uint32 size;
	uint32 ucode_offset;
	uint32 ucode_size;
	uint32 data_offset;
	uint32 data_size;
	uint64 ucode_start;
	uint64 data_start;
	uint32 version;
	bool staged;
	uint64 gpu;
	area_id payload_area;
	void* payload_cpu;
	phys_addr_t payload_phys;
	uint64 payload_gpu;
	uint32 payload_size;
};

struct rdna4_sdma_ring {
	area_id area;
	uint32* ring_cpu;
	phys_addr_t ring_phys;
	uint64 ring_gpu;
	area_id rptr_area;
	volatile uint64* rptr_cpu;
	phys_addr_t rptr_phys;
	uint64 rptr_gpu;
	area_id wptr_area;
	volatile uint64* wptr_cpu;
	phys_addr_t wptr_phys;
	uint64 wptr_gpu;
	uint32 wptr;
	bool ready;
};

struct rdna4_mes_state {
	area_id ring_area;
	uint32* ring_cpu;
	phys_addr_t ring_phys;
	uint64 ring_gpu;
	area_id status_area;
	volatile uint64* status_cpu;
	phys_addr_t status_phys;
	uint64 status_gpu;
	uint32 wptr;
	uint32 completion_seq;
	uint32 last_irq_data;
	bool ready;
};

struct rdna4_vcn_state {
	area_id ring_area;
	uint32* ring_cpu;
	uint64 ring_gpu;
	area_id work_area;
	void* work_cpu;
	uint64 work_gpu;
	uint32 wptr;
	uint32 ring_dwords;
	bool ready;
};

struct rdna4_device {
	int32 id;
	pci_info* pci;
	uint32 device_id;
	uint32 gfx_ip;
	uint32 revision;

	addr_t mmio_phys;
	addr_t doorbell_phys;
	size_t mmio_size;
	size_t doorbell_size;
	uint8* mmio;
	volatile uint32* doorbell;
	area_id mmio_area;
	area_id doorbell_area;

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
	bool mmhub_ready;
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
	area_id psp_fw_area;
	area_id psp_boot_area;
	void* psp_boot_cpu;
	phys_addr_t psp_boot_phys;
	size_t psp_boot_size;
	void* psp_fw_cpu;
	phys_addr_t psp_fw_phys;
	uint32 psp_fw_size;
	uint32 psp_sos_offset;
	uint32 psp_sos_size;
	uint32 psp_fw_type;
	uint32 psp_fw_version_major;
	uint32 psp_fw_version_minor;
	area_id psp_ring_area;
	area_id psp_cmd_area;
	area_id psp_fence_area;
	void* psp_ring_cpu;
	void* psp_cmd_cpu;
	volatile uint32* psp_fence_cpu;
	phys_addr_t psp_ring_phys;
	phys_addr_t psp_cmd_phys;
	phys_addr_t psp_fence_phys;
	uint64 psp_ring_gpu;
	uint64 psp_cmd_gpu;
	uint64 psp_fence_gpu;
	uint32 psp_fence_value;
	bool psp_ring_ready;
	area_id psp_tmr_area;
	void* psp_tmr_cpu;
	phys_addr_t psp_tmr_phys;
	uint64 psp_tmr_gpu;
	uint32 psp_tmr_size;
	sem_id fence_sem;
	bool irq_installed;
	area_id ih_area;
	uint32* ih_cpu;
	phys_addr_t ih_phys;
	uint64 ih_gpu;
	uint32 ih_rptr;
	bool ih_enabled;
	volatile uint64 interrupt_count;
	volatile uint64 vm_fault_count;
	volatile uint64 vm_fault_address;
	volatile uint32 vm_fault_status;
	volatile uint32 vm_fault_vmid;
	volatile uint32 mes_last_irq_data;
	rdna4_discovery_state discovery;
	rdna4_firmware_slot firmware[RDNA4_FW_MAX];
	rdna4_sdma_ring sdma[2];
	rdna4_mes_state mes;
	rdna4_vcn_state vcn;
	area_id gfx_mqd_area;
	void* gfx_mqd_cpu;
	phys_addr_t gfx_mqd_phys;
	rdna4_bo gfx_ring_bo;
	rdna4_vm_table vm_tables[RDNA4_VM_MAX_TABLES];
	rdna4_bo bos[RDNA4_VM_MAX_BOS];
};

extern pci_module_info* gPCI;
extern mutex gDriverLock;
extern rdna4_device* gDevices[RDNA4_MAX_CARDS];
extern char* gDeviceNames[RDNA4_MAX_CARDS + 1];
extern device_hooks gDeviceHooks;


static inline void rdna4_doorbell_write(rdna4_device& d, uint32 index, uint64 value)
{
	if (d.doorbell == NULL || ((uint64)index * 8 + 8) > d.doorbell_size)
		return;
	*((volatile uint64*)((uint8*)d.doorbell + (uint64)index * 8)) = value;
}

status_t rdna4_init(rdna4_device& device);
void rdna4_uninit(rdna4_device& device);
status_t rdna4_ioctl(rdna4_device& device, uint32 op, void* buffer,
	size_t length);

#endif
