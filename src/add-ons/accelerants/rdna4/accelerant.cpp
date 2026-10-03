#include "rdna4.h"
#include "accelerant_protos.h"

#include <OS.h>
#include <stdio.h>
#include <string.h>

static int sFD = -1;
static area_id sSharedArea = -1;
static area_id sFramebufferArea = -1;
static rdna4_shared_info* sShared = NULL;
static uint8* sFramebuffer = NULL;
static display_mode sModes[4];
static uint32 sModeCount = 0;

static display_mode
make_mode(uint16 width, uint16 height, uint32 pixelClock,
	uint16 hStart, uint16 hEnd, uint16 hTotal,
	uint16 vStart, uint16 vEnd, uint16 vTotal)
{
	display_mode m = {};
	m.space = B_RGB32_LITTLE;
	m.virtual_width = width;
	m.virtual_height = height;
	m.h_display_start = 0;
	m.v_display_start = 0;
	m.flags = rdna4_mode_flags();
	m.timing.pixel_clock = pixelClock;
	m.timing.h_display = width;
	m.timing.h_sync_start = hStart;
	m.timing.h_sync_end = hEnd;
	m.timing.h_total = hTotal;
	m.timing.v_display = height;
	m.timing.v_sync_start = vStart;
	m.timing.v_sync_end = vEnd;
	m.timing.v_total = vTotal;
	return m;
}

static void
build_mode_list()
{
	sModeCount = 0;
	sModes[sModeCount++] = make_mode(1280, 720, 74250, 1390, 1430, 1650,
		725, 730, 750);
	sModes[sModeCount++] = make_mode(1920, 1080, 148500, 2008, 2052, 2200,
		1084, 1089, 1125);
	sModes[sModeCount++] = make_mode(2560, 1440, 241500, 2608, 2640, 2720,
		1443, 1448, 1481);
	sModes[sModeCount++] = make_mode(3840, 2160, 533250, 4016, 4104, 4400,
		2168, 2178, 2250);
}

status_t
rdna4_init_accelerant(int fd)
{
	sFD = fd;
	rdna4_private_data data = {};
	data.magic = RDNA4_PRIVATE_DATA_MAGIC;
	if (ioctl(sFD, RDNA4_GET_PRIVATE_DATA, &data, sizeof(data)) != B_OK)
		return B_ERROR;

	sSharedArea = clone_area("rdna4 shared", (void**)&sShared, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, data.shared_area);
	if (sSharedArea < 0)
		return sSharedArea;

	sFramebufferArea = clone_area("rdna4 framebuffer", (void**)&sFramebuffer,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		sShared->framebuffer_area);
	if (sFramebufferArea < 0) {
		delete_area(sSharedArea);
		sSharedArea = -1;
		sShared = NULL;
		return sFramebufferArea;
	}

	build_mode_list();
	if (sModeCount != 0)
		sShared->current_mode = sModes[1];
	sShared->bits_per_pixel = 32;
	sShared->bytes_per_row = sShared->current_mode.virtual_width * 4;
	return B_OK;
}

void
rdna4_uninit_accelerant(void)
{
	if (sFramebufferArea >= 0)
		delete_area(sFramebufferArea);
	if (sSharedArea >= 0)
		delete_area(sSharedArea);
	sFramebufferArea = sSharedArea = -1;
	sFramebuffer = NULL;
	sShared = NULL;
	sFD = -1;
}

status_t
rdna4_get_accelerant_device_info(accelerant_device_info* info)
{
	if (info == NULL || sShared == NULL)
		return B_BAD_VALUE;
	memset(info, 0, sizeof(*info));
	info->version = B_ACCELERANT_VERSION;
	strlcpy(info->name, RDNA4_ACCELERANT_NAME, sizeof(info->name));
	snprintf(info->chipset, sizeof(info->chipset), "AMD RDNA4 GFX12.%u",
		sShared->gfx_ip == 0x1201 ? 1 : 0);
	strlcpy(info->serial_no, "native-rdna4", sizeof(info->serial_no));
	info->memory = sShared->vram_size;
	return B_OK;
}

uint32 rdna4_accelerant_mode_count(void) { return sModeCount; }

status_t
rdna4_get_mode_list(display_mode* list)
{
	if (list == NULL)
		return B_BAD_VALUE;
	memcpy(list, sModes, sizeof(display_mode) * sModeCount);
	return B_OK;
}

status_t
rdna4_propose_display_mode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	if (target == NULL)
		return B_BAD_VALUE;
	display_mode proposed = *target;
	proposed.flags |= rdna4_mode_flags();
	if (proposed.space != B_RGB32_LITTLE)
		proposed.space = B_RGB32_LITTLE;

	for (uint32 i = 0; i < sModeCount; i++) {
		if (sModes[i].virtual_width == proposed.virtual_width
			&& sModes[i].virtual_height == proposed.virtual_height) {
			proposed.timing = sModes[i].timing;
			*target = proposed;
			return B_OK;
		}
	}

	if (low != NULL && proposed.virtual_width < low->virtual_width)
		proposed = *low;
	if (high != NULL && proposed.virtual_width > high->virtual_width)
		proposed = *high;

	for (uint32 i = 0; i < sModeCount; i++) {
		if (sModes[i].virtual_width == proposed.virtual_width
			&& sModes[i].virtual_height == proposed.virtual_height) {
			*target = sModes[i];
			return B_OK;
		}
	}
	return B_BAD_VALUE;
}

status_t
rdna4_set_display_mode(display_mode* mode)
{
	if (sShared == NULL || mode == NULL)
		return B_NO_INIT;

	display_mode selected = *mode;
	if (rdna4_propose_display_mode(&selected, NULL, NULL) != B_OK)
		return B_BAD_VALUE;

	uint32 bpp = selected.space == B_RGB16_LITTLE ? 2 : 4;
	uint64 required = (uint64)selected.virtual_width * selected.virtual_height * bpp;
	if (required > sShared->framebuffer_size)
		return B_NO_MEMORY;

	sShared->current_mode = selected;
	sShared->bits_per_pixel = bpp * 8;
	sShared->bytes_per_row = selected.virtual_width * bpp;
	*mode = selected;
	return B_OK;
}

status_t
rdna4_get_display_mode(display_mode* mode)
{
	if (sShared == NULL || mode == NULL)
		return B_NO_INIT;
	*mode = sShared->current_mode;
	return B_OK;
}

status_t
rdna4_get_preferred_display_mode(display_mode* mode)
{
	if (mode == NULL || sModeCount < 2)
		return B_BAD_VALUE;
	*mode = sModes[1];
	return B_OK;
}

status_t
rdna4_get_frame_buffer_config(frame_buffer_config* config)
{
	if (sShared == NULL || config == NULL)
		return B_NO_INIT;
	config->frame_buffer = sFramebuffer;
	config->frame_buffer_dma = (uint8*)sShared->framebuffer_phys;
	config->bytes_per_row = sShared->bytes_per_row;
	return B_OK;
}

status_t
rdna4_get_pixel_clock_limits(display_mode*, uint32* low, uint32* high)
{
	if (low) *low = 25000;
	if (high) *high = 1200000;
	return B_OK;
}

uint32 rdna4_dpms_capabilities(void)
{
	return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}

uint32 rdna4_dpms_mode(void)
{
	return sShared ? B_DPMS_ON : B_DPMS_OFF;
}

status_t
rdna4_set_dpms_mode(uint32 mode)
{
	if (!sShared)
		return B_NO_INIT;
	(void)mode;
	return B_OK;
}
