#include "accelerant.h"

#include <Accelerant.h>

extern "C" void* get_accelerant_hook(uint32 feature, void* data)
{
	(void)data;

	switch (feature) {
		case B_INIT_ACCELERANT:
			return NULL;
		case B_UNINIT_ACCELERANT:
			return NULL;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return NULL;
		case B_ACCELERANT_MODE_COUNT:
			return NULL;
		case B_GET_MODE_LIST:
			return NULL;
		case B_SET_DISPLAY_MODE:
			return NULL;
		case B_GET_DISPLAY_MODE:
			return NULL;
		case B_DPMS_CAPABILITIES:
			return NULL;
		case B_DPMS_MODE:
			return NULL;
		case B_SET_DPMS_MODE:
			return NULL;
	}

	return NULL;
}
