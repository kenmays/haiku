#include "accelerant.h"
#include "accelerant_protos.h"

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int sFD = -1;
static display_mode sMode;
static bool sInitialized = false;
static area_id sSharedArea = -1;

rdna4_shared_info* gSharedInfo = NULL;

status_t
amdgpu_rdna4_init_accelerant(int fd)
{
	rdna4_get_private_data data;
	data.magic = RDNA4_PRIVATE_DATA_MAGIC;
	data.shared_info_area = -1;
	if (ioctl(fd, RDNA4_GET_PRIVATE_DATA, &data, sizeof(data)) != 0)
		return errno;

	sFD = fd;
	sSharedArea = data.shared_info_area;
	area_info info;
	if (get_area_info(sSharedArea, &info) != B_OK)
		return B_ERROR;
	gSharedInfo = (rdna4_shared_info*)info.address;
	if (gSharedInfo == NULL || !gSharedInfo->initialized)
		return B_ERROR;

	memset(&sMode, 0, sizeof(sMode));
	sMode.virtual_width = gSharedInfo->framebuffer_width;
	sMode.virtual_height = gSharedInfo->framebuffer_height;
	sMode.space = B_RGB32_LITTLE;
	sInitialized = true;
	return B_OK;
}

void
amdgpu_rdna4_uninit_accelerant()
{
	if (sSharedArea >= 0)
		delete_area(sSharedArea);
	sSharedArea = -1;
	gSharedInfo = NULL;
	sFD = -1;
	sInitialized = false;
}

status_t
amdgpu_rdna4_get_device_info(accelerant_device_info* info)
{
	if (info == NULL || !sInitialized)
		return B_BAD_VALUE;
	memset(info, 0, sizeof(*info));
	return B_OK;
}

uint32
amdgpu_rdna4_mode_count()
{
	return sInitialized && gSharedInfo->framebuffer_fallback ? 1 : 0;
}

status_t
amdgpu_rdna4_get_mode_list(display_mode* modes)
{
	if (!sInitialized || modes == NULL)
		return B_BAD_VALUE;
	if (!gSharedInfo->framebuffer_fallback)
		return B_ENTRY_NOT_FOUND;
	memset(modes, 0, sizeof(display_mode));
	modes->virtual_width = gSharedInfo->framebuffer_width;
	modes->virtual_height = gSharedInfo->framebuffer_height;
	modes->space = B_RGB32_LITTLE;
	modes->timing.h_display = gSharedInfo->framebuffer_width;
	modes->timing.v_display = gSharedInfo->framebuffer_height;
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
	if (mode->virtual_width != gSharedInfo->framebuffer_width
		|| mode->virtual_height != gSharedInfo->framebuffer_height)
		return B_NOT_SUPPORTED;
	sMode = *mode;
	return B_OK;
}

uint32
amdgpu_rdna4_dpms_capabilities()
{
	return B_DPMS_ON | B_DPMS_OFF;
}

uint32
amdgpu_rdna4_dpms_mode()
{
	return B_DPMS_ON;
}

status_t
amdgpu_rdna4_set_dpms_mode(uint32 mode)
{
	return mode == B_DPMS_ON || mode == B_DPMS_OFF ? B_OK : B_BAD_VALUE;
}
