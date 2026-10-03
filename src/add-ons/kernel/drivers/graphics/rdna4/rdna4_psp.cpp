#include "rdna4_psp.h"
#include "driver.h"
#include "rdna4_vm.h"

#include <KernelExport.h>
#include <OS.h>
#include <string.h>

namespace {
static const uint32 kRingBytes = 4096;
static const uint32 kFrameBytes = 64;
static const uint32 kFrameDwords = kFrameBytes / 4;
static const uint32 kRingDwords = kRingBytes / 4;

static const uint32 C2P64 = 0x16080;
static const uint32 C2P67 = 0x16083;
static const uint32 C2P69 = 0x16085;
static const uint32 C2P70 = 0x16086;
static const uint32 C2P71 = 0x16087;

static const uint32 GFX_CTRL_INIT_GPCOM = 0x00020000;
static const uint32 GFX_CTRL_DESTROY_RINGS = 0x00030000;
static const uint32 GFX_CMD_LOAD_IP_FW = 0x00000006;
static const uint32 GFX_CMD_AUTOLOAD_RLC = 0x00000021;
static const uint32 PSP_GFX_CMD_BUF_VERSION = 1;
static const uint32 GFX_FLAG_RESPONSE = 0x80000000;

struct psp_cmd_load_ip_fw {
	uint32 fw_lo;
	uint32 fw_hi;
	uint32 fw_size;
	uint32 fw_type;
};

struct psp_cmd_buffer {
	uint32 buf_size;
	uint32 buf_version;
	uint32 cmd_id;
	uint32 resp_buf_lo;
	uint32 resp_buf_hi;
	uint32 resp_offset;
	uint32 resp_buf_size;
	uint8 command[836];
	uint32 status;
	uint32 session_id;
	uint32 fw_addr_lo;
	uint32 fw_addr_hi;
	uint32 tmr_size;
	uint32 reserved[11];
	uint8 response_union[32];
	uint8 tail[64];
} __attribute__((packed));

struct psp_ring_frame {
	uint32 cmd_lo;
	uint32 cmd_hi;
	uint32 cmd_size;
	uint32 fence_lo;
	uint32 fence_hi;
	uint32 fence_value;
	uint32 sid_lo;
	uint32 sid_hi;
	uint8 vmid;
	uint8 frame_type;
	uint8 reserved[2];
	uint32 reserved2[7];
} __attribute__((packed));

static inline uint32 reg_read(rdna4_device& d, uint32 reg)
{
	return *(volatile uint32*)(d.mmio + ((size_t)reg << 2));
}

static inline void reg_write(rdna4_device& d, uint32 reg, uint32 value)
{
	*(volatile uint32*)(d.mmio + ((size_t)reg << 2)) = value;
	(void)reg_read(d, reg);
}

static status_t wait_reg(rdna4_device& d, uint32 reg, uint32 mask,
	uint32 value, bigtime_t timeout)
{
	bigtime_t deadline = system_time() + timeout;
	do {
		if ((reg_read(d, reg) & mask) == value)
			return B_OK;
		snooze(20);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}

static status_t alloc_page(rdna4_device& d, const char* name, area_id& area,
	void*& cpu, phys_addr_t& phys, uint64& gpu, uint32 bytes)
{
	area = -1;
	cpu = NULL;
	phys = 0;
	gpu = 0;
	size_t size = (bytes + B_PAGE_SIZE - 1) & ~(size_t)(B_PAGE_SIZE - 1);
	area = create_area(name, &cpu, B_ANY_KERNEL_ADDRESS, size, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < 0)
		return area;

	physical_entry entry;
	status_t status = get_memory_map(cpu, size, &entry, 1);
	if (status != B_OK || entry.size < size) {
		delete_area(area);
		area = -1;
		return status != B_OK ? status : B_NOT_SUPPORTED;
	}

	phys = entry.address;
	rdna4_bo bo = {};
	bo.area = area;
	bo.cpu = cpu;
	bo.size = size;
	bo.alignment = B_PAGE_SIZE;
	bo.physical = phys;
	bo.used = true;
	status = rdna4_vm_map_bo(d, bo, B_PAGE_SIZE);
	if (status != B_OK) {
		delete_area(area);
		area = -1;
		return status;
	}
	gpu = bo.gpu;
	return B_OK;
}

static void free_page(rdna4_device& d, area_id area, void* cpu,
	phys_addr_t phys, uint64 gpu, uint32 bytes)
{
	if (area < 0)
		return;
	rdna4_bo bo = {};
	bo.area = area;
	bo.cpu = cpu;
	bo.size = (bytes + B_PAGE_SIZE - 1) & ~(uint64)(B_PAGE_SIZE - 1);
	bo.physical = phys;
	bo.gpu = gpu;
	bo.used = true;
	if (d.vm_ready && gpu)
		rdna4_vm_unmap_bo(d, bo);
	delete_area(area);
}

static uint32 ring_wptr(rdna4_device& d)
{
	return reg_read(d, C2P67) % kRingDwords;
}

static status_t submit_frame(rdna4_device& d, psp_ring_frame frame)
{
	if (!d.psp_ring_ready || d.psp_ring_cpu == NULL)
		return B_NO_INIT;

	uint32 wptr = ring_wptr(d);
	if ((wptr % kFrameDwords) != 0)
		return B_BAD_VALUE;
	uint32 index = wptr / kFrameDwords;
	if (index >= kRingBytes / kFrameBytes)
		return B_BAD_VALUE;

	psp_ring_frame* ring = (psp_ring_frame*)d.psp_ring_cpu;
	ring[index] = frame;
	d.psp_fence_value++;
	frame.fence_value = d.psp_fence_value;
	((psp_ring_frame*)d.psp_ring_cpu)[index] = frame;
	__sync_synchronize();
	uint32 next = (wptr + kFrameDwords) % kRingDwords;
	reg_write(d, C2P67, next);

	bigtime_t deadline = system_time() + 5000000;
	while (system_time() < deadline) {
		if (d.psp_fence_cpu != NULL && *d.psp_fence_cpu == d.psp_fence_value)
			return B_OK;
		snooze(50);
	}
	return B_TIMED_OUT;
}
}

status_t
rdna4_psp_init(rdna4_device& d)
{
	d.psp_ring_area = -1;
	d.psp_cmd_area = -1;
	d.psp_fence_area = -1;
	d.psp_ring_cpu = NULL;
	d.psp_cmd_cpu = NULL;
	d.psp_fence_cpu = NULL;
	d.psp_ring_phys = d.psp_cmd_phys = d.psp_fence_phys = 0;
	d.psp_ring_gpu = d.psp_cmd_gpu = d.psp_fence_gpu = 0;
	d.psp_fence_value = 0;
	d.psp_ring_ready = false;
	return B_OK;
}

status_t
rdna4_psp_ring_create(rdna4_device& d)
{
	if (d.mmio == NULL || !d.vm_ready || d.psp_ring_ready)
		return d.psp_ring_ready ? B_OK : B_NO_INIT;

	status_t status = alloc_page(d, "rdna4 PSP ring", d.psp_ring_area,
		(void*&)d.psp_ring_cpu, d.psp_ring_phys, d.psp_ring_gpu, kRingBytes);
	if (status != B_OK)
		return status;
	status = alloc_page(d, "rdna4 PSP command", d.psp_cmd_area,
		d.psp_cmd_cpu, d.psp_cmd_phys, d.psp_cmd_gpu, B_PAGE_SIZE);
	if (status != B_OK) {
		rdna4_psp_ring_destroy(d);
		return status;
	}
	status = alloc_page(d, "rdna4 PSP fence", d.psp_fence_area,
		(void*&)d.psp_fence_cpu, d.psp_fence_phys, d.psp_fence_gpu, B_PAGE_SIZE);
	if (status != B_OK) {
		rdna4_psp_ring_destroy(d);
		return status;
	}

	memset(d.psp_ring_cpu, 0, kRingBytes);
	memset(d.psp_cmd_cpu, 0, B_PAGE_SIZE);
	*d.psp_fence_cpu = 0;
	d.psp_fence_value = 0;

	/* PSP v14 non-SR-IOV uses C2PMSG_69..71 for the GPCOM ring and
	 * C2PMSG_64 for the ring command/response handshake. */
	status = wait_reg(d, C2P64, 0x80000000u, 0x80000000u, 500000);
	if (status != B_OK) {
		rdna4_psp_ring_destroy(d);
		return status;
	}
	reg_write(d, C2P69, (uint32)d.psp_ring_gpu);
	reg_write(d, C2P70, (uint32)(d.psp_ring_gpu >> 32));
	reg_write(d, C2P71, kRingBytes);
	reg_write(d, C2P64, GFX_CTRL_INIT_GPCOM << 16);
	snooze(20000);
	status = wait_reg(d, C2P64, GFX_FLAG_RESPONSE, GFX_FLAG_RESPONSE, 500000);
	if (status != B_OK) {
		rdna4_psp_ring_destroy(d);
		return status;
	}
	reg_write(d, C2P67, 0);
	d.psp_ring_ready = true;
	return B_OK;
}

void
rdna4_psp_ring_destroy(rdna4_device& d)
{
	if (d.psp_ring_ready && d.mmio != NULL) {
		reg_write(d, C2P64, GFX_CTRL_DESTROY_RINGS);
		snooze(20000);
		(void)wait_reg(d, C2P64, GFX_FLAG_RESPONSE, GFX_FLAG_RESPONSE, 500000);
	}
	d.psp_ring_ready = false;
	free_page(d, d.psp_fence_area, (void*)d.psp_fence_cpu, d.psp_fence_phys,
		d.psp_fence_gpu, B_PAGE_SIZE);
	free_page(d, d.psp_cmd_area, d.psp_cmd_cpu, d.psp_cmd_phys,
		d.psp_cmd_gpu, B_PAGE_SIZE);
	free_page(d, d.psp_ring_area, d.psp_ring_cpu, d.psp_ring_phys,
		d.psp_ring_gpu, kRingBytes);
	d.psp_fence_area = d.psp_cmd_area = d.psp_ring_area = -1;
	d.psp_ring_cpu = d.psp_cmd_cpu = NULL;
	d.psp_fence_cpu = NULL;
}

status_t
rdna4_psp_load_ip_firmware(rdna4_device& d, uint32 type, uint32 pspType)
{
	if (!d.psp_ring_ready || type >= RDNA4_FW_MAX)
		return B_NO_INIT;
	rdna4_firmware_slot& fw = d.firmware[type];
	if (!fw.staged || fw.gpu == 0 || fw.size == 0)
		return B_ENTRY_NOT_FOUND;

	memset(d.psp_cmd_cpu, 0, B_PAGE_SIZE);
	psp_cmd_buffer* cmd = (psp_cmd_buffer*)d.psp_cmd_cpu;
	cmd->buf_size = sizeof(psp_cmd_buffer);
	cmd->buf_version = PSP_GFX_CMD_BUF_VERSION;
	cmd->cmd_id = GFX_CMD_LOAD_IP_FW;
	psp_cmd_load_ip_fw load = {};
	load.fw_lo = (uint32)fw.gpu;
	load.fw_hi = (uint32)(fw.gpu >> 32);
	load.fw_size = fw.size;
	load.fw_type = pspType;
	memcpy(cmd->command, &load, sizeof(load));

	psp_ring_frame frame = {};
	frame.cmd_lo = (uint32)d.psp_cmd_gpu;
	frame.cmd_hi = (uint32)(d.psp_cmd_gpu >> 32);
	frame.cmd_size = sizeof(psp_cmd_buffer);
	frame.fence_lo = (uint32)d.psp_fence_gpu;
	frame.fence_hi = (uint32)(d.psp_fence_gpu >> 32);
	frame.vmid = 0;

	status_t status = submit_frame(d, frame);
	if (status != B_OK)
		return status;

	if (cmd->status != 0)
		return B_ERROR;
	return B_OK;
}

status_t
rdna4_psp_load_firmware(rdna4_device& d)
{
	status_t status = rdna4_psp_ring_create(d);
	if (status != B_OK)
		return status;

	struct Mapping { uint32 type; uint32 psp; };
	static const Mapping map[] = {
		{RDNA4_FW_GFX_PFP, 2},
		{RDNA4_FW_GFX_ME, 1},
		{RDNA4_FW_GFX_MEC, 4},
		{RDNA4_FW_GFX_RLC, 8},
		{RDNA4_FW_SDMA0, 9},
		{RDNA4_FW_SDMA1, 10},
		{RDNA4_FW_VCN, 13},
		{RDNA4_FW_SMU, 18},
		{RDNA4_FW_MES, 33},
		{RDNA4_FW_MES1, 81},
	};
	for (uint32 i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (!d.firmware[map[i].type].staged)
			continue;
		status = rdna4_psp_load_ip_firmware(d, map[i].type, map[i].psp);
		if (status != B_OK)
			return status;
	}
	/* Once all supplied GFX images have been accepted, request RLC autoload.
	 * PSP performs signature validation before the firmware becomes active. */
	if (d.firmware[RDNA4_FW_GFX_RLC].staged) {
		memset(d.psp_cmd_cpu, 0, B_PAGE_SIZE);
		psp_cmd_buffer* cmd = (psp_cmd_buffer*)d.psp_cmd_cpu;
		cmd->buf_size = sizeof(psp_cmd_buffer);
		cmd->buf_version = PSP_GFX_CMD_BUF_VERSION;
		cmd->cmd_id = GFX_CMD_AUTOLOAD_RLC;
		psp_ring_frame frame = {};
		frame.cmd_lo = (uint32)d.psp_cmd_gpu;
		frame.cmd_hi = (uint32)(d.psp_cmd_gpu >> 32);
		frame.cmd_size = sizeof(psp_cmd_buffer);
		frame.fence_lo = (uint32)d.psp_fence_gpu;
		frame.fence_hi = (uint32)(d.psp_fence_gpu >> 32);
		frame.vmid = 0;
		status = submit_frame(d, frame);
		if (status != B_OK)
			return status;
		if (cmd->status != 0)
			return B_ERROR;
	}
	if (d.shared) {
		d.shared->psp_state = RDNA4_ENGINE_RUNNING;
		d.shared->gfx_state = RDNA4_ENGINE_FIRMWARE_READY;
		d.shared->sdma_state = RDNA4_ENGINE_FIRMWARE_READY;
		d.shared->mes_state = RDNA4_ENGINE_FIRMWARE_READY;
		d.shared->smu_state = RDNA4_ENGINE_FIRMWARE_READY;
	}
	return B_OK;
}

void
rdna4_psp_uninit(rdna4_device& d)
{
	rdna4_psp_ring_destroy(d);
	d.psp_ring_area = d.psp_cmd_area = d.psp_fence_area = -1;
	d.psp_ring_cpu = d.psp_cmd_cpu = NULL;
	d.psp_fence_cpu = NULL;
}
