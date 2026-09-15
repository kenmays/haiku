#ifndef RDNA4_FIRMWARE_H
#define RDNA4_FIRMWARE_H

#include <SupportDefs.h>

#define RDNA4_FIRMWARE_MAX_NAME 96

enum rdna4_firmware_type {
	RDNA4_FW_PFP = 0,
	RDNA4_FW_ME,
	RDNA4_FW_MEC,
	RDNA4_FW_RLC,
	RDNA4_FW_TOC,
	RDNA4_FW_DMCUB
};

struct rdna4_firmware_image {
	const void* data;
	size_t size;
	uint32 version;
	uint32 feature_version;
	bool valid;
};

struct rdna4_firmware_set {
	rdna4_firmware_image pfp;
	rdna4_firmware_image me;
	rdna4_firmware_image mec;
	rdna4_firmware_image rlc;
	rdna4_firmware_image toc;
	rdna4_firmware_image dmcub;
	bool rs64_enabled;
};

status_t rdna4_firmware_parse(const void* data, size_t size,
	rdna4_firmware_image& image);
status_t rdna4_firmware_validate(const rdna4_firmware_set& firmware);

/* Returns the Linux-compatible firmware basename for the selected GFX12
 * generation. The caller owns the returned static string. */
const char* rdna4_firmware_name(rdna4_firmware_type type,
	uint32 gfx_version);

#endif
