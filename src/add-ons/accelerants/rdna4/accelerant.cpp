#include "rdna4.h"
#include <Accelerant.h>
#include <OS.h>
#include <string.h>

static area_id sSharedArea = -1;
static rdna4_shared_info* sShared = NULL;

static status_t init_accelerant(int fd)
{
	rdna4_private_data p = {};
	p.magic = RDNA4_PRIVATE_DATA_MAGIC;
	if (ioctl(fd, RDNA4_GET_PRIVATE_DATA, &p, sizeof(p)) != B_OK)
		return B_ERROR;
	sSharedArea = clone_area("rdna4 shared", (void**)&sShared, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, p.shared_area);
	return sSharedArea < 0 ? sSharedArea : B_OK;
}

static void uninit_accelerant(void)
{
	if (sSharedArea >= 0) delete_area(sSharedArea);
	sSharedArea = -1; sShared = NULL;
}

static status_t get_display_mode(display_mode* mode)
{
	if (!sShared || !mode) return B_NO_INIT;
	*mode = sShared->current_mode;
	mode->flags |= B_HARDWARE_CURSOR | B_PARALLEL_ACCESS | B_DPMS;
	return B_OK;
}

static status_t set_display_mode(const display_mode* mode)
{
	if (!sShared || !mode) return B_NO_INIT;
	sShared->current_mode = *mode;
	sShared->current_mode.flags |= B_HARDWARE_CURSOR | B_PARALLEL_ACCESS | B_DPMS;
	sShared->bits_per_pixel = 32;
	sShared->bytes_per_row = mode->virtual_width * 4;
	return B_OK;
}

static void* get_accelerant_hook(uint32 feature)
{
	switch (feature) {
	case B_INIT_ACCELERANT: return (void*)init_accelerant;
	case B_UNINIT_ACCELERANT: return (void*)uninit_accelerant;
	case B_ACCELERANT_CLONE_INFO_SIZE: return NULL;
	case B_ACCELERANT_RESTRICTIONS: return NULL;
	case B_ACCELERANT_MODE_COUNT: return NULL;
	case B_ACCELERANT_GET_MODE_LIST: return NULL;
	case B_ACCELERANT_PROPOSE_DISPLAY_MODE: return NULL;
	case B_ACCELERANT_SET_DISPLAY_MODE: return (void*)set_display_mode;
	default: return NULL;
	}
}

extern "C" void* get_accelerant_hook(uint32 feature) { return ::get_accelerant_hook(feature); }
