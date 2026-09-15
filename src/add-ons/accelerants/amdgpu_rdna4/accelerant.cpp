#include "accelerant.h"
#include "accelerant_protos.h"

#include <string.h>

static int sFD = -1;
static display_mode sMode;
static bool sInitialized = false;

rdna4_shared_info* gSharedInfo = NULL;

status_t
amdgpu_rdna4_init_accelerant(int fd)
{
	sFD = fd;
	memset(&sMode, 0, sizeof(sMode));
	sInitialized = true;
	return B_OK;
}

void
amdgpu_rdna4_uninit_accelerant()
{
	sFD = -1;
	sInitialized = false;
	gSharedInfo = NULL;
}

status_t
amdgpu_rdna4_get_device_info(accelerant_device_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;
	memset(info, 0, sizeof(*info));
	return B_OK;
}

uint32
amdgpu_rdna4_mode_count()
{
	return 0;
}

status_t
amdgpu_rdna4_get_mode_list(display_mode* modes)
{
	(void)modes;
	return B_NOT_SUPPORTED;
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
	(void)mode;
	return B_NOT_SUPPORTED;
}

uint32
amdgpu_rdna4_dpms_capabilities()
{
	return 0;
}

uint32
amdgpu_rdna4_dpms_mode()
{
	return B_DPMS_OFF;
}

status_t
amdgpu_rdna4_set_dpms_mode(uint32 mode)
{
	(void)mode;
	return B_NOT_SUPPORTED;
}
