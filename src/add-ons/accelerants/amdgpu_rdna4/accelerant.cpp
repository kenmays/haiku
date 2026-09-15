#include "accelerant.h"
#include "accelerant_protos.h"

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int sFD = -1;
static area_id sSharedArea = -1;
static display_mode sMode;
static bool sInitialized = false;
static uint32 sModeCount = 0;
static rdna4_shared_info* sShared = NULL;

rdna4_shared_info* gSharedInfo = NULL;

status_t
amdgpu_rdna4_init_accelerant(int fd)
{
	if (fd < 0)
		return B_BAD_VALUE;

	rdna4_get_private_data data;
	data.magic = RDNA4_PRIVATE_DATA_MAGIC;
	data.shared_info_area = -1;
	if (ioctl(fd, RDNA4_GET_PRIVATE_DATA, &data, sizeof(data)) < 0)
		return B_ERROR;

	if (data.shared_info_area < 0)
		return B_ERROR;

	void* address = NULL;
	if (area_for(data.shared_info_area, &address) != B_OK)
		return B_ERROR;

	sFD = fd;
	sSharedArea = data.shared_info_area;
	sShared = (rdna4_shared_info*)address;
	gSharedInfo = sShared;
	memset(&sMode, 0, sizeof(sMode));

	/* Boot-framebuffer mode is exposed when firmware supplied valid data. */
	if (sShared->framebuffer_width != 0 && sShared->framebuffer_height != 0
		&& sShared->framebuffer_pitch != 0) {
		sMode.virtual_width = sShared->framebuffer_width;
		sMode.virtual_height = sShared->framebuffer_height;
		sMode.space = B_RGB32_LITTLE;
		sMode.timing.h_display = sShared->framebuffer_width;
		sMode.timing.v_display = sShared->framebuffer_height;
		sMode.timing.pixel_clock = 0;
		sModeCount = 1;
	} else
		sModeCount = 0;

	sInitialized = true;
	return B_OK;
}

void
amdgpu_rdna4_uninit_accelerant()
{
	if (sSharedArea >= 0)
		delete_area(sSharedArea);
	sSharedArea = -1;
	sShared = NULL;
	gSharedInfo = NULL;
	sFD = -1;
	sModeCount = 0;
	sInitialized = false;
}

status_t
amdgpu_rdna4_get_device_info(accelerant_device_info* info)
{
	if (!sInitialized || info == NULL)
		return B_BAD_VALUE;
	memset(info, 0, sizeof(*info));
	strlcpy(info->name, "AMD Radeon RDNA4", sizeof(info->name));
	return B_OK;
}

uint32
amdgpu_rdna4_mode_count()
{
	return sModeCount;
}

status_t
amdgpu_rdna4_get_mode_list(display_mode* modes)
{
	if (!sInitialized || modes == NULL)
		return B_BAD_VALUE;
	if (sModeCount == 0)
		return B_ENTRY_NOT_FOUND;
	modes[0] = sMode;
	return B_OK;
}

status_t
amdgpu_rdna4_get_display_mode(display_mode* mode)
{
	if (!sInitialized || mode == NULL)
		return B_BAD_VALUE;
	*mode = sMode;
	return B_OK;
}

status_t
amdgpu_rdna4_set_display_mode(display_mode* mode)
{
	if (!sInitialized || mode == NULL)
		return B_BAD_VALUE;
	if (sModeCount == 0 || mode->virtual_width != sMode.virtual_width
		|| mode->virtual_height != sMode.virtual_height
		|| mode->space != sMode.space)
		return B_NOT_SUPPORTED;
	return B_OK;
}

uint32
amdgpu_rdna4_dpms_capabilities()
{
	return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}

uint32
amdgpu_rdna4_dpms_mode()
{
	return sInitialized ? B_DPMS_ON : B_DPMS_OFF;
}

status_t
amdgpu_rdna4_set_dpms_mode(uint32 mode)
{
	if (!sInitialized)
		return B_NO_INIT;
	if (mode != B_DPMS_ON && mode != B_DPMS_STAND_BY
		&& mode != B_DPMS_SUSPEND && mode != B_DPMS_OFF)
		return B_BAD_VALUE;
	/* Hardware DPMS is intentionally deferred until DCN4 ownership. */
	return mode == B_DPMS_ON ? B_OK : B_NOT_SUPPORTED;
}
