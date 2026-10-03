#include "rdna4_gfx.h"

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
rdna4_pm4_release_mem(uint32* out, uint64 address, uint64 value)
{
	if (out == NULL)
		return 0;
	out[0] = RDNA4_PM4_PACKET3(RDNA4_PM4_RELEASE_MEM, 7);
	out[1] = 0;
	out[2] = 0;
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

	d.gfx_ring_area = area;
	d.gfx_ring_cpu = address;
	d.gfx_ring_phys = entry.address;
	d.gfx_ring_gpu = d.gfx_ring_bo.gpu;
	d.gfx_ring_dwords = (uint32)(size / sizeof(uint32));
	d.gfx_ring_wptr = 0;
	d.gfx_ring_rptr = 0;
	d.gfx_ring_ready = false;

	return B_OK;
}

void
rdna4_gfx_ring_free(rdna4_device& d)
{
	if (d.gfx_ring_bo.used)
		rdna4_vm_unmap_bo(d, d.gfx_ring_bo);
	if (d.gfx_ring_area >= 0)
		delete_area(d.gfx_ring_area);

	memset(&d.gfx_ring_bo, 0, sizeof(d.gfx_ring_bo));
	d.gfx_ring_area = -1;
	d.gfx_ring_cpu = NULL;
	d.gfx_ring_phys = 0;
	d.gfx_ring_gpu = 0;
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
	return B_OK;
}

status_t
rdna4_gfx_program_ring(rdna4_device& d)
{
	if (!d.gfx_ring_bo.used)
		return B_NO_INIT;
	if (d.shared == NULL || d.shared->gfx_state != RDNA4_ENGINE_FIRMWARE_READY)
		return B_NOT_INITIALIZED;

	/*
	 * The CP ring is 64 KiB, expressed to hardware as a 2^n dword
	 * buffer-size mask. Ring 0 is the sole GFX12 graphics ring.
	 */
	rdna4_write_reg(d, RDNA4_CP_RB0_BASE,
		(uint32)(d.gfx_ring_gpu >> 8));
	rdna4_write_reg(d, RDNA4_CP_RB0_CNTL, 0x0000003f);
	rdna4_write_reg(d, RDNA4_CP_RB0_RPTR_ADDR,
		(uint32)(d.gfx_ring_gpu >> 2));
	rdna4_write_reg(d, RDNA4_CP_RB0_RPTR_ADDR_HI,
		(uint32)(d.gfx_ring_gpu >> 34));
	rdna4_write_reg(d, RDNA4_CP_RB0_BUFSZ_MASK, 0x3f);
	rdna4_write_reg(d, RDNA4_CP_RB0_WPTR, 0);
	rdna4_write_reg(d, RDNA4_CP_RB0_WPTR_HI, 0);
	rdna4_write_reg(d, RDNA4_CP_RB_DOORBELL_CONTROL, 0);

	d.gfx_ring_rptr = 0;
	d.gfx_ring_wptr = 0;
	d.gfx_ring_ready = true;
	return B_OK;
}
