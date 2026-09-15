#include "rdna4_firmware.h"

#include <string.h>

status_t
rdna4_firmware_parse(const void* data, size_t size, rdna4_firmware_image& image)
{
	memset(&image, 0, sizeof(image));
	if (data == NULL || size == 0)
		return B_BAD_VALUE;

	/* Keep parsing opaque until the exact Haiku firmware ABI and the GFX12
	 * firmware container used by the target ASIC are available.  We retain
	 * the image verbatim so a later loader can consume it without another
	 * ownership transition. */
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
	const char* suffix = gfx_version == 0x1200 ? "gfx1200" : "gfx1201";
	switch (type) {
		case RDNA4_FW_PFP: return suffix;
		case RDNA4_FW_ME: return suffix;
		case RDNA4_FW_MEC: return suffix;
		case RDNA4_FW_RLC: return suffix;
		case RDNA4_FW_TOC: return suffix;
		case RDNA4_FW_DMCUB: return "dcn";
	}
	return NULL;
}
