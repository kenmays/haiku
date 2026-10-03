#include "rdna4_gfx.h"
#include <graphics/rdna4/rdna4.h>

static inline bool
is_privileged_opcode(uint32 opcode)
{
	/*
	 * Register writes and context-control packets are not accepted from an
	 * untrusted user command stream. The native ABI will construct those
	 * packets in kernel-owned submission paths.
	 */
	switch (opcode) {
		case RDNA4_PM4_WRITE_DATA:
		case RDNA4_PM4_RELEASE_MEM:
		case RDNA4_PM4_INDIRECT_BUFFER:
			return true;
		default:
			return false;
	}
}

uint32
rdna4_pm4_nop(uint32* out, uint32 count)
{
	if (out == NULL || count == 0)
		return 0;
	out[0] = RDNA4_PM4_PACKET3(RDNA4_PM4_NOP, count);
	for (uint32 i = 1; i < count; i++)
		out[i] = 0;
	return count;
}

uint32
rdna4_pm4_write_data(uint32* out, uint64 address, uint32 value)
{
	if (out == NULL)
		return 0;
	out[0] = RDNA4_PM4_PACKET3(RDNA4_PM4_WRITE_DATA, 4);
	out[1] = 0;
	out[2] = (uint32)address;
	out[3] = (uint32)(address >> 32);
	out[4] = value;
	return 5;
}

uint32
rdna4_pm4_release_mem(uint32* out, uint64 address, uint64 value, uint32 gfx_ip)
{
	if (out == NULL)
		return 0;

	/* GFX12 RELEASE_MEM is a type-3 packet with seven payload dwords. */
	out[0] = RDNA4_PM4_PACKET3(RDNA4_PM4_RELEASE_MEM, 6);

	const uint32 eventType = 0x14; /* CACHE_FLUSH_AND_INV_TS_EVENT */
	const uint32 eventIndex = 5;   /* end-of-pipe timestamp */
	if (gfx_ip == RDNA4_GFX12_1) {
		/* GFX12.1 uses the revised GCR/temporal encoding. */
		out[1] = (1u << 22) | (1u << 24) | (2u << 12)
			| (3u << 25) | eventType | (eventIndex << 8);
	} else {
		/* GFX12.0 encoding used by gc_12_0_0. */
		out[1] = (1u << 22) | (1u << 21) | (3u << 25)
			| eventType | (eventIndex << 8);
	}
	/* 64-bit fence write + interrupt when the write is confirmed. */
	out[2] = (2u << 29) | (2u << 24);
	out[3] = (uint32)address;
	out[4] = (uint32)(address >> 32);
	out[5] = (uint32)value;
	out[6] = (uint32)(value >> 32);
	out[7] = 0;
	return 8;
}

uint32
rdna4_pm4_indirect_buffer(uint32* out, uint64 address, uint32 size_dw)
{
	if (out == NULL || size_dw == 0)
		return 0;
	out[0] = RDNA4_PM4_PACKET3(RDNA4_PM4_INDIRECT_BUFFER, 3);
	out[1] = (uint32)address;
	out[2] = (uint32)(address >> 32);
	out[3] = size_dw;
	return 4;
}

status_t
rdna4_validate_command_buffer(const rdna4_command_buffer& command)
{
	if (command.words == NULL || command.word_count == 0)
		return B_BAD_VALUE;
	if (command.word_count > (16u * 1024u * 1024u))
		return B_BAD_VALUE;

	uint32 i = 0;
	while (i < command.word_count) {
		uint32 header = command.words[i];
		if ((header >> 30) != RDNA4_PM4_TYPE3)
			return B_BAD_DATA;

		uint32 opcode = (header >> 8) & 0xff;
		uint32 count = (header & 0x3fff) + 1;
		if (count == 0 || i + 1 + count > command.word_count)
			return B_BAD_DATA;

		if (is_privileged_opcode(opcode))
			return B_NOT_ALLOWED;

		i += 1 + count;
	}
	return B_OK;
}

#include "driver.h"
#include "rdna4_vm.h"

#include <KernelExport.h>
#include <OS.h>
#include <string.h>

/* GFX12 register offsets are dword indices from the GC register block.
   These values correspond to gc_12_0_0 and are shared by GFX12.0/GFX12.0.1. */
#define RDNA4_CP_RB0_BASE                 0x1de0
#define RDNA4_CP_RB0_CNTL                 0x1de1
#define RDNA4_CP_RB0_RPTR                 0x0f60
#define RDNA4_CP_RB0_WPTR                 0x1df4
#define RDNA4_CP_RB0_WPTR_HI              0x1df5
#define RDNA4_CP_RB0_RPTR_ADDR            0x1de3
#define RDNA4_CP_RB0_RPTR_ADDR_HI         0x1de4
#define RDNA4_CP_RB0_BUFSZ_MASK           0x1de5
#define RDNA4_CP_RB_DOORBELL_CONTROL      0x1e8d
#define RDNA4_CP_RB_DOORBELL_RANGE_LOWER    0x1e8f
#define RDNA4_CP_RB_DOORBELL_RANGE_UPPER    0x1e90
#define RDNA4_GFX_DOORBELL_INDEX            0x08b
#define RDNA4_CP_RB_DOORBELL_OFFSET_SHIFT   2
#define RDNA4_CP_RB_DOORBELL_ENABLE        (1u << 30)
#define RDNA4_CP_RB_ACTIVE                 0x1e8e
#define RDNA4_CP_RB0_BASE_HI              0x1e51
#define RDNA4_CP_RB_WPTR_POLL_ADDR_LO     0x1e8b
#define RDNA4_CP_RB_WPTR_POLL_ADDR_HI     0x1e8c
#define RDNA4_CP_RB_VMID                  0x1df1
/* GFX12 CP MQD/HQD registers (dword indices). */
#define RDNA4_CP_GFX_MQD_BASE_ADDR        0x1e7e
#define RDNA4_CP_GFX_MQD_BASE_ADDR_HI     0x1e7f
#define RDNA4_CP_GFX_HQD_ACTIVE            0x1e80
#define RDNA4_CP_GFX_HQD_VMID              0x1e81
#define RDNA4_CP_GFX_HQD_BASE              0x1e86
#define RDNA4_CP_GFX_HQD_BASE_HI           0x1e87
#define RDNA4_CP_GFX_HQD_RPTR              0x1e88
#define RDNA4_CP_GFX_HQD_RPTR_ADDR         0x1e89
#define RDNA4_CP_GFX_HQD_RPTR_ADDR_HI      0x1e8a
#define RDNA4_CP_GFX_HQD_CNTL              0x1e8f
#define RDNA4_CP_GFX_HQD_WPTR              0x1e91
#define RDNA4_CP_GFX_HQD_WPTR_HI           0x1e92
#define RDNA4_CP_GFX_MQD_CONTROL_DEFAULT   0x00000100u
#define RDNA4_CP_GFX_HQD_VMID_DEFAULT      0x00000000u
#define RDNA4_CP_GFX_HQD_PRIORITY_DEFAULT  0x00000000u
#define RDNA4_CP_GFX_HQD_QUANTUM_DEFAULT   0x00000a01u
#define RDNA4_CP_GFX_HQD_CNTL_DEFAULT      0x00f00000u
#define RDNA4_CP_HQD_EOP_CONTROL_DEFAULT  0x00000006u
#define RDNA4_CP_HQD_PQ_CONTROL_DEFAULT    0x00308509u
#define RDNA4_CP_HQD_PERSISTENT_DEFAULT    0x0be05501u
#define RDNA4_CP_HQD_IB_CONTROL_DEFAULT    0x00300000u


static inline void
rdna4_write_reg(rdna4_device& d, uint32 reg, uint32 value)
{
	*(volatile uint32*)(d.mmio + ((uint64)reg << 2)) = value;
}

static inline uint32
rdna4_read_reg(rdna4_device& d, uint32 reg)
{
	return *(volatile uint32*)(d.mmio + ((uint64)reg << 2));
}

status_t
rdna4_gfx_ring_alloc(rdna4_device& d)
{
	if (d.gfx_ring_ready)
		return B_OK;
	if (d.mmio == NULL || !d.vm_ready)
		return B_NO_INIT;

	const size_t size = 64 * 1024;
	void* address = NULL;
	area_id area = create_area("rdna4 gfx ring", &address,
		B_ANY_KERNEL_ADDRESS, size, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < 0)
		return area;

	physical_entry entry;
	status_t status = get_memory_map(address, size, &entry, 1);
	if (status != B_OK || entry.size < size) {
		delete_area(area);
		return status != B_OK ? status : B_NOT_SUPPORTED;
	}

	memset(address, 0, size);
	memset(&d.gfx_ring_bo, 0, sizeof(d.gfx_ring_bo));
	d.gfx_ring_bo.area = area;
	d.gfx_ring_bo.cpu = address;
	d.gfx_ring_bo.size = size;
	d.gfx_ring_bo.alignment = B_PAGE_SIZE;
	d.gfx_ring_bo.physical = entry.address;
	d.gfx_ring_bo.flags = 0;
	d.gfx_ring_bo.used = true;

	status = rdna4_vm_map_bo(d, d.gfx_ring_bo, B_PAGE_SIZE);
	if (status != B_OK) {
		d.gfx_ring_bo.used = false;
		delete_area(area);
		return status;
	}

	void* wbAddress = NULL;
	area_id wbArea = create_area("rdna4 gfx ring writeback", &wbAddress,
		B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (wbArea < 0) {
		rdna4_vm_unmap_bo(d, d.gfx_ring_bo);
		d.gfx_ring_bo.used = false;
		delete_area(area);
		return wbArea;
	}

	physical_entry wbEntry;
	status = get_memory_map(wbAddress, B_PAGE_SIZE, &wbEntry, 1);
	if (status != B_OK || wbEntry.size < B_PAGE_SIZE) {
		delete_area(wbArea);
		rdna4_vm_unmap_bo(d, d.gfx_ring_bo);
		d.gfx_ring_bo.used = false;
		delete_area(area);
		return status != B_OK ? status : B_NOT_SUPPORTED;
	}
	memset(wbAddress, 0, B_PAGE_SIZE);

	rdna4_bo wbBO = {};
	wbBO.area = wbArea;
	wbBO.cpu = wbAddress;
	wbBO.size = B_PAGE_SIZE;
	wbBO.alignment = B_PAGE_SIZE;
	wbBO.physical = wbEntry.address;
	wbBO.used = true;
	status = rdna4_vm_map_bo(d, wbBO, B_PAGE_SIZE);
	if (status != B_OK) {
		delete_area(wbArea);
		rdna4_vm_unmap_bo(d, d.gfx_ring_bo);
		d.gfx_ring_bo.used = false;
		delete_area(area);
		return status;
	}

	d.gfx_ring_area = area;
	d.gfx_ring_cpu = address;
	d.gfx_ring_phys = entry.address;
	d.gfx_ring_gpu = d.gfx_ring_bo.gpu;
	d.gfx_ring_dwords = (uint32)(size / sizeof(uint32));
	d.gfx_ring_rptr_area = wbArea;
	d.gfx_ring_rptr_cpu = (volatile uint32*)wbAddress;
	d.gfx_ring_rptr_phys = wbEntry.address;
	d.gfx_ring_rptr_gpu = wbBO.gpu;
	d.gfx_ring_wptr_poll_cpu = (volatile uint32*)((uint8*)wbAddress + 4);
	d.gfx_ring_wptr_poll_gpu = wbBO.gpu + 4;
	d.gfx_ring_wptr = 0;
	d.gfx_ring_rptr = 0;
	d.gfx_ring_ready = false;

	/* The writeback BO remains private to the kernel ring. */
	return B_OK;
}

void
rdna4_gfx_ring_free(rdna4_device& d)
{
	if (d.gfx_ring_rptr_cpu != NULL && d.gfx_ring_rptr_area >= 0) {
		rdna4_bo wb = {};
		wb.area = d.gfx_ring_rptr_area;
		wb.cpu = (void*)d.gfx_ring_rptr_cpu;
		wb.size = B_PAGE_SIZE;
		wb.gpu = d.gfx_ring_rptr_gpu;
		wb.physical = d.gfx_ring_rptr_phys;
		wb.used = true;
		rdna4_vm_unmap_bo(d, wb);
		delete_area(d.gfx_ring_rptr_area);
	}
	if (d.gfx_ring_bo.used)
		rdna4_vm_unmap_bo(d, d.gfx_ring_bo);
	if (d.gfx_ring_area >= 0)
		delete_area(d.gfx_ring_area);
	if (d.gfx_mqd_bo.used) {
		rdna4_vm_unmap_bo(d, d.gfx_mqd_bo);
		d.gfx_mqd_bo = {};
	}
	if (d.gfx_mqd_area >= 0)
		delete_area(d.gfx_mqd_area);
	d.gfx_mqd_area = -1;
	d.gfx_mqd_cpu = NULL;
	d.gfx_mqd_phys = 0;
	d.gfx_mqd_gpu = 0;

	memset(&d.gfx_ring_bo, 0, sizeof(d.gfx_ring_bo));
	d.gfx_ring_area = -1;
	d.gfx_ring_cpu = NULL;
	d.gfx_ring_phys = 0;
	d.gfx_ring_gpu = 0;
	d.gfx_ring_rptr_area = -1;
	d.gfx_ring_rptr_cpu = NULL;
	d.gfx_ring_rptr_phys = 0;
	d.gfx_ring_rptr_gpu = 0;
	d.gfx_ring_wptr_poll_cpu = NULL;
	d.gfx_ring_wptr_poll_gpu = 0;
	d.gfx_ring_dwords = 0;
	d.gfx_ring_wptr = d.gfx_ring_rptr = 0;
	d.gfx_ring_ready = false;
}

status_t
rdna4_gfx_ring_write(rdna4_device& d, const uint32* packets, size_t dwords)
{
	if (!d.gfx_ring_bo.used || packets == NULL || dwords == 0
		|| dwords >= d.gfx_ring_dwords)
		return B_BAD_VALUE;

	/* Refresh the hardware read pointer from the kernel-only writeback page. */
	if (d.gfx_ring_rptr_cpu != NULL)
		d.gfx_ring_rptr = *d.gfx_ring_rptr_cpu & (d.gfx_ring_dwords - 1);

	/* Ring management deliberately refuses to overwrite unread commands. */
	uint32 next = (d.gfx_ring_wptr + (uint32)dwords) & (d.gfx_ring_dwords - 1);
	if (d.gfx_ring_wptr < d.gfx_ring_rptr) {
		if (next >= d.gfx_ring_rptr)
			return B_WOULD_BLOCK;
	} else if (next < d.gfx_ring_wptr && next >= d.gfx_ring_rptr) {
		return B_WOULD_BLOCK;
	}

	uint32* ring = (uint32*)d.gfx_ring_cpu;
	for (size_t i = 0; i < dwords; i++)
		ring[(d.gfx_ring_wptr + i) & (d.gfx_ring_dwords - 1)] = packets[i];

	__sync_synchronize();
	d.gfx_ring_wptr = next;
	return B_OK;
}

status_t
rdna4_gfx_ring_kick(rdna4_device& d)
{
	if (!d.gfx_ring_bo.used)
		return B_NO_INIT;

	/*
	 * Hardware programming is only permitted after CP firmware/MQD
	 * initialization. The ring allocation and GPUVA are nevertheless
	 * complete and can be inspected before firmware bring-up.
	 */
	if (!d.gfx_ring_ready)
		return B_NOT_INITIALIZED;

	rdna4_write_reg(d, RDNA4_CP_RB0_WPTR, d.gfx_ring_wptr);
	rdna4_write_reg(d, RDNA4_CP_RB0_WPTR_HI, 0);
	rdna4_doorbell_write(d, RDNA4_GFX_DOORBELL_INDEX, d.gfx_ring_wptr);
	return B_OK;
}

status_t
rdna4_gfx_program_ring(rdna4_device& d)
{
	if (!d.gfx_ring_bo.used)
		return B_NO_INIT;
	if (d.shared == NULL || d.shared->gfx_state != RDNA4_ENGINE_FIRMWARE_READY)
		return B_NOT_INITIALIZED;

	status_t mqdStatus = rdna4_gfx_program_mqd(d);
	if (mqdStatus != B_OK)
		return mqdStatus;

	/*
	 * The CP ring is 64 KiB, expressed to hardware as a 2^n dword
	 * buffer-size mask. Ring 0 is the sole GFX12 graphics ring.
	 */
	const uint32 rbBufSize = 13; /* log2(64 KiB / 8) */
	const uint32 rbBlockSize = rbBufSize - 2;
	rdna4_write_reg(d, RDNA4_CP_RB_VMID, 0);
	rdna4_write_reg(d, RDNA4_CP_RB0_CNTL,
		rbBufSize | (rbBlockSize << 8));
	rdna4_write_reg(d, RDNA4_CP_RB0_WPTR, 0);
	rdna4_write_reg(d, RDNA4_CP_RB0_WPTR_HI, 0);
	rdna4_write_reg(d, RDNA4_CP_RB0_RPTR_ADDR,
		(uint32)d.gfx_ring_rptr_gpu);
	rdna4_write_reg(d, RDNA4_CP_RB0_RPTR_ADDR_HI,
		(uint32)(d.gfx_ring_rptr_gpu >> 32) & 0xffff);
	rdna4_write_reg(d, RDNA4_CP_RB_WPTR_POLL_ADDR_LO,
		(uint32)d.gfx_ring_wptr_poll_gpu);
	rdna4_write_reg(d, RDNA4_CP_RB_WPTR_POLL_ADDR_HI,
		(uint32)(d.gfx_ring_wptr_poll_gpu >> 32));
uint64 rbAddr = d.gfx_ring_gpu >> 8;
	rdna4_write_reg(d, RDNA4_CP_RB0_BASE, (uint32)rbAddr);
	rdna4_write_reg(d, RDNA4_CP_RB0_BASE_HI, (uint32)(rbAddr >> 32));
	rdna4_write_reg(d, RDNA4_CP_RB_ACTIVE, 1);
	rdna4_write_reg(d, RDNA4_CP_RB_DOORBELL_CONTROL,
		(RDNA4_GFX_DOORBELL_INDEX << RDNA4_CP_RB_DOORBELL_OFFSET_SHIFT)
		| RDNA4_CP_RB_DOORBELL_ENABLE);
	rdna4_write_reg(d, RDNA4_CP_RB_DOORBELL_RANGE_LOWER,
		RDNA4_GFX_DOORBELL_INDEX << RDNA4_CP_RB_DOORBELL_OFFSET_SHIFT);
	rdna4_write_reg(d, RDNA4_CP_RB_DOORBELL_RANGE_UPPER, 0x0000fffc);

	d.gfx_ring_rptr = 0;
	d.gfx_ring_wptr = 0;
	d.gfx_ring_ready = true;
	return B_OK;
}

status_t
rdna4_gfx_program_mqd(rdna4_device& d)
{
	if (!d.gfx_ring_bo.used || d.mmio == NULL)
		return B_NO_INIT;
	if (d.shared == NULL || d.shared->gfx_state != RDNA4_ENGINE_FIRMWARE_READY)
		return B_NOT_INITIALIZED;

	if (d.gfx_mqd_area < 0) {
		void* address = NULL;
		d.gfx_mqd_area = create_area("rdna4 gfx mqd", &address,
			B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_CONTIGUOUS,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (d.gfx_mqd_area < 0)
			return d.gfx_mqd_area;
		physical_entry entry;
		status_t status = get_memory_map(address, B_PAGE_SIZE, &entry, 1);
		if (status != B_OK || entry.size < B_PAGE_SIZE) {
			delete_area(d.gfx_mqd_area);
			d.gfx_mqd_area = -1;
			return status != B_OK ? status : B_NOT_SUPPORTED;
		}
		memset(address, 0, B_PAGE_SIZE);
		d.gfx_mqd_cpu = address;
		d.gfx_mqd_phys = entry.address;
		d.gfx_mqd_bo = {};
		d.gfx_mqd_bo.area = d.gfx_mqd_area;
		d.gfx_mqd_bo.cpu = address;
		d.gfx_mqd_bo.size = B_PAGE_SIZE;
		d.gfx_mqd_bo.physical = entry.address;
		d.gfx_mqd_bo.alignment = B_PAGE_SIZE;
		d.gfx_mqd_bo.used = true;
		status_t mapStatus = rdna4_vm_map_bo(d, d.gfx_mqd_bo, B_PAGE_SIZE);
		if (mapStatus != B_OK) {
			delete_area(d.gfx_mqd_area);
			d.gfx_mqd_area = -1;
			d.gfx_mqd_cpu = NULL;
			d.gfx_mqd_phys = 0;
			return mapStatus;
		}
		d.gfx_mqd_gpu = d.gfx_mqd_bo.gpu;
	}

	/* Program the GFX12 single graphics queue's MQD/HQD defaults. */
	rdna4_write_reg(d, RDNA4_CP_GFX_MQD_BASE_ADDR,
		(uint32)(d.gfx_mqd_gpu >> 8));
	rdna4_write_reg(d, RDNA4_CP_GFX_MQD_BASE_ADDR_HI,
		(uint32)(d.gfx_mqd_gpu >> 40));
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_ACTIVE, 0);
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_VMID,
		RDNA4_CP_GFX_HQD_VMID_DEFAULT);
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_CNTL,
		RDNA4_CP_GFX_HQD_CNTL_DEFAULT);
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_RPTR, 0);
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_RPTR_ADDR,
		(uint32)d.gfx_ring_rptr_gpu);
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_RPTR_ADDR_HI,
		(uint32)(d.gfx_ring_rptr_gpu >> 32));
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_WPTR, 0);
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_WPTR_HI, 0);
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_BASE,
		(uint32)(d.gfx_ring_gpu >> 8));
	rdna4_write_reg(d, RDNA4_CP_GFX_HQD_BASE_HI,
		(uint32)(d.gfx_ring_gpu >> 40));

	/* These defaults are represented in the MQD image for firmware/HQD load. */
	uint32* mqd = (uint32*)d.gfx_mqd_cpu;
	mqd[0] = RDNA4_CP_GFX_MQD_CONTROL_DEFAULT;
	mqd[1] = RDNA4_CP_GFX_HQD_VMID_DEFAULT;
	mqd[2] = RDNA4_CP_GFX_HQD_PRIORITY_DEFAULT;
	mqd[3] = RDNA4_CP_GFX_HQD_QUANTUM_DEFAULT;
	mqd[4] = RDNA4_CP_HQD_EOP_CONTROL_DEFAULT;
	mqd[5] = RDNA4_CP_HQD_PQ_CONTROL_DEFAULT;
	mqd[6] = RDNA4_CP_HQD_PERSISTENT_DEFAULT;
	mqd[7] = RDNA4_CP_HQD_IB_CONTROL_DEFAULT;
	__sync_synchronize();

	return B_OK;
}
