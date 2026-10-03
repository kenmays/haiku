/*
 * Native AMD RDNA4 Haiku graphics ABI.
 *
 * The kernel owns PCI/MMIO, memory management, firmware, interrupts,
 * GPUVM, queues and reset. The accelerant owns Haiku display/engine hooks.
 */
#ifndef RDNA4_H
#define RDNA4_H

#include <Accelerant.h>
#include <Drivers.h>
#include <PCI.h>
#include <SupportDefs.h>

#define RDNA4_VENDOR_ID 0x1002
#define RDNA4_DEVICE_NAVI44 0x7550
#define RDNA4_DEVICE_NAVI48_ALT 0x7551
#define RDNA4_DEVICE_NAVI44_MOBILE 0x7590

#define RDNA4_GFX12_0 0x1200
#define RDNA4_GFX12_1 0x1201

#define RDNA4_PRIVATE_DATA_MAGIC 'r4hd'
#define RDNA4_ACCELERANT_NAME "rdna4.accelerant"
#define RDNA4_DEVICE_NAME "rdna4"
#define RDNA4_MAX_CARDS 4

enum rdna4_ip_block {
	RDNA4_IP_COMMON = 0,
	RDNA4_IP_GMC, RDNA4_IP_IH, RDNA4_IP_PSP, RDNA4_IP_SMU,
	RDNA4_IP_DISPLAY, RDNA4_IP_GFX, RDNA4_IP_MES,
	RDNA4_IP_SDMA0, RDNA4_IP_SDMA1, RDNA4_IP_VCN, RDNA4_IP_MAX
};

enum rdna4_feature {
	RDNA4_FEATURE_DISPLAY		= 1ull << 0,
	RDNA4_FEATURE_CURSOR		= 1ull << 1,
	RDNA4_FEATURE_VRAM		= 1ull << 2,
	RDNA4_FEATURE_GTT		= 1ull << 3,
	RDNA4_FEATURE_IOMMU		= 1ull << 4,
	RDNA4_FEATURE_GFX_QUEUE		= 1ull << 5,
	RDNA4_FEATURE_COMPUTE_QUEUE	= 1ull << 6,
	RDNA4_FEATURE_SDMA		= 1ull << 7,
	RDNA4_FEATURE_MES		= 1ull << 8,
	RDNA4_FEATURE_HOTPLUG		= 1ull << 9,
	RDNA4_FEATURE_DP		= 1ull << 10,
	RDNA4_FEATURE_HDMI		= 1ull << 11,
	RDNA4_FEATURE_AUDIO		= 1ull << 12,
	RDNA4_FEATURE_POWER		= 1ull << 13,
	RDNA4_FEATURE_RESET		= 1ull << 14,
	RDNA4_FEATURE_RAS		= 1ull << 15,
	RDNA4_FEATURE_VIDEO_DECODE	= 1ull << 16,
	RDNA4_FEATURE_VIDEO_ENCODE	= 1ull << 17,
	RDNA4_FEATURE_3D		= 1ull << 18
};

enum rdna4_engine_state {
	RDNA4_ENGINE_OFF = 0,
	RDNA4_ENGINE_DISCOVERED,
	RDNA4_ENGINE_FIRMWARE_READY,
	RDNA4_ENGINE_VM_READY,
	RDNA4_ENGINE_SCHEDULER_READY,
	RDNA4_ENGINE_RUNNING,
	RDNA4_ENGINE_RESETTING,
	RDNA4_ENGINE_FAILED
};

struct rdna4_gpu_info {
	uint32 version;
	uint32 gfx_ip;
	uint32 device_id;
	uint32 revision;
	uint32 cu_count;
	uint32 wave_size;
	uint64 vram_size;
	uint64 gtt_size;
	uint64 feature_mask;
	uint32 gfx_state;
	uint32 display_state;
	uint32 psp_state;
	uint32 smu_state;
	uint32 mes_state;
	uint32 sdma_state;
	uint64 reset_generation;
};

struct rdna4_private_data {
	uint32 magic;
	area_id shared_area;
};

struct rdna4_shared_info {
	uint32 version;
	uint32 device_id;
	uint32 revision;
	uint32 gfx_ip;
	uint32 dcn_ip;
	uint32 cu_count;
	uint32 wave_size;
	uint64 feature_mask;
	uint64 vram_size;
	uint64 gtt_size;

	area_id registers_area;
	area_id framebuffer_area;
	area_id status_area;
	area_id command_area;

	phys_addr_t registers_phys;
	phys_addr_t framebuffer_phys;
	size_t registers_size;
	size_t framebuffer_size;

	display_mode current_mode;
	uint32 bytes_per_row;
	uint32 bits_per_pixel;

	volatile uint64 gpu_reset_generation;
	volatile uint32 gfx_state;
	volatile uint32 display_state;
	volatile uint32 psp_state;
	volatile uint32 smu_state;
	volatile uint32 mes_state;
	volatile uint32 sdma_state;
	volatile uint32 vm_state;
};

enum {
	RDNA4_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	RDNA4_GET_GPU_INFO,
	RDNA4_ALLOCATE_BUFFER,
	RDNA4_FREE_BUFFER,
	RDNA4_SUBMIT_GFX,
	RDNA4_WAIT_FENCE,
	RDNA4_RESET_GPU,
	RDNA4_GET_FIRMWARE_INFO,
	RDNA4_GET_VM_INFO,
	RDNA4_WAIT_IDLE
};

struct rdna4_buffer_request {
	uint32 magic;
	uint32 flags;
	uint64 size;
	uint64 alignment;
	uint64 gpu_address;
	area_id area;
	phys_addr_t physical_address;
};

struct rdna4_submit {
	uint32 magic;
	uint32 queue;
	uint32 command_count;
	uint32 flags;
	uint64 command_gpu_address;
	uint64 fence_gpu_address;
	uint64 fence_value;
};

struct rdna4_wait_idle {
	bigtime_t timeout;
};

struct rdna4_wait_fence {
	uint32 magic;
	uint32 reserved;
	uint64 fence_gpu_address;
	uint64 fence_value;
	bigtime_t timeout;
};

struct rdna4_firmware_info {
	uint32 gfx_ip;
	uint32 psp_ip;
	uint32 smu_ip;
	uint32 sdma_ip;
	uint32 mes_ip;
	uint32 flags;
	char gfx_pfp[64];
	char gfx_me[64];
	char gfx_mec[64];
	char gfx_rlc[64];
	char gfx_rlc_kicker[64];
	char gfx_toc[64];
	char psp_sos[64];
	char psp_toc[64];
	char smu[64];
	char gfx_imu[64];
	char mes[64];
	char mes1[64];
	char uni_mes[64];
	char sdma0[64];
	char sdma1[64];
};

struct rdna4_vm_info {
	uint32 version;
	uint32 page_shift;
	uint32 pde_levels;
	uint32 vmid_count;
	uint64 va_bits;
	uint64 page_table_flags;
};

static inline uint32
rdna4_mode_flags()
{
	return B_PARALLEL_ACCESS | B_DPMS;
}

#endif
