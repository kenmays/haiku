#include "rdna4_firmware.h"

#include <string.h>

status_t
rdna4_firmware_parse(const void* data, size_t size, rdna4_firmware_image& image)
{
	memset(&image, 0, sizeof(image));
	if (data == NULL || size == 0)
		return B_BAD_VALUE;

	/* The image remains owned by the caller.  Parsing the generation-specific
	 * firmware header belongs in the hardware loader once the target Haiku
	 * firmware ABI is selected. */
	image.data = data;
	image.size = size;
	image.valid = true;
	return B_OK;
}

status_t
rdna4_firmware_validate(const rdna4_firmware_set& firmware)
{
	if (!firmware.pfp.valid || firmware.pfp.data == NULL || firmware.pfp.size == 0)
		return B_BAD_VALUE;
	if (!firmware.me.valid || firmware.me.data == NULL || firmware.me.size == 0)
		return B_BAD_VALUE;
	if (!firmware.mec.valid || firmware.mec.data == NULL || firmware.mec.size == 0)
		return B_BAD_VALUE;
	if (!firmware.rlc.valid || firmware.rlc.data == NULL || firmware.rlc.size == 0)
		return B_BAD_VALUE;
	if (!firmware.toc.valid || firmware.toc.data == NULL || firmware.toc.size == 0)
		return B_BAD_VALUE;
	return B_OK;
}

const char*
rdna4_firmware_name(rdna4_firmware_type type, uint32 gfx_version)
{
	const char* suffix;
	switch (gfx_version) {
		case 0x1200:
			suffix = "gfx1200";
			break;
		case 0x1201:
			suffix = "gfx1201";
			break;
		default:
			return NULL;
	}

	switch (type) {
		case RDNA4_FW_PFP: {
			static const char pfp1200[] = "amdgpu/gfx1200_pfp.bin";
			static const char pfp1201[] = "amdgpu/gfx1201_pfp.bin";
			return suffix[3] == '0' ? pfp1200 : pfp1201;
		}
		case RDNA4_FW_ME: {
			static const char me1200[] = "amdgpu/gfx1200_me.bin";
			static const char me1201[] = "amdgpu/gfx1201_me.bin";
			return suffix[3] == '0' ? me1200 : me1201;
		}
		case RDNA4_FW_MEC: {
			static const char mec1200[] = "amdgpu/gfx1200_mec.bin";
			static const char mec1201[] = "amdgpu/gfx1201_mec.bin";
			return suffix[3] == '0' ? mec1200 : mec1201;
		}
		case RDNA4_FW_RLC: {
			static const char rlc1200[] = "amdgpu/gfx1200_rlc.bin";
			static const char rlc1201[] = "amdgpu/gfx1201_rlc.bin";
			return suffix[3] == '0' ? rlc1200 : rlc1201;
		}
		case RDNA4_FW_TOC: {
			static const char toc1200[] = "amdgpu/gfx1200_toc.bin";
			static const char toc1201[] = "amdgpu/gfx1201_toc.bin";
			return suffix[3] == '0' ? toc1200 : toc1201;
		}
		case RDNA4_FW_DMCUB:
			/* DMCUB filenames are DCN-revision-specific; do not guess a
			 * revision from the GFX version. */
			return NULL;
	}
	return NULL;
}
