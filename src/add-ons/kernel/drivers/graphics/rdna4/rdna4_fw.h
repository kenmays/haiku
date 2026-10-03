#ifndef RDNA4_FW_H
#define RDNA4_FW_H

#include <SupportDefs.h>

struct rdna4_device;

enum rdna4_firmware_type {
	RDNA4_FW_PSP = 0,
	RDNA4_FW_GFX_PFP,
	RDNA4_FW_GFX_ME,
	RDNA4_FW_GFX_MEC,
	RDNA4_FW_GFX_RLC,
	RDNA4_FW_GFX_RLC_KICKER,
	RDNA4_FW_GFX_IMU,
	RDNA4_FW_MES,
	RDNA4_FW_MES1,
	RDNA4_FW_UNI_MES,
	RDNA4_FW_SDMA0,
	RDNA4_FW_SDMA1,
	RDNA4_FW_SMU,
	RDNA4_FW_MAX
};

struct rdna4_firmware_stage {
	uint32 magic;
	uint32 type;
	uint64 user_address;
	uint32 size;
	uint32 flags;
	uint32 psp_sos_offset;
	uint32 psp_sos_size;
	uint32 psp_version_major;
	uint32 psp_version_minor;
	uint32 reserved;
};

status_t rdna4_firmware_stage(rdna4_device& device,
	const rdna4_firmware_stage& request);
status_t rdna4_firmware_boot(rdna4_device& device);
void rdna4_firmware_uninit(rdna4_device& device);

#endif
