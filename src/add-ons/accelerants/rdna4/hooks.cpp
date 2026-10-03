#include "accelerant_protos.h"

extern "C" void*
get_accelerant_hook(uint32 feature)
{
	switch (feature) {
		case B_INIT_ACCELERANT:
			return (void*)rdna4_init_accelerant;
		case B_UNINIT_ACCELERANT:
			return (void*)rdna4_uninit_accelerant;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return (void*)rdna4_get_accelerant_device_info;

		case B_ACCELERANT_MODE_COUNT:
			return (void*)rdna4_accelerant_mode_count;
		case B_GET_MODE_LIST:
			return (void*)rdna4_get_mode_list;
		case B_PROPOSE_DISPLAY_MODE:
			return (void*)rdna4_propose_display_mode;
		case B_SET_DISPLAY_MODE:
			return (void*)rdna4_set_display_mode;
		case B_GET_DISPLAY_MODE:
			return (void*)rdna4_get_display_mode;
		case B_GET_PREFERRED_DISPLAY_MODE:
			return (void*)rdna4_get_preferred_display_mode;
		case B_GET_FRAME_BUFFER_CONFIG:
			return (void*)rdna4_get_frame_buffer_config;
		case B_GET_PIXEL_CLOCK_LIMITS:
			return (void*)rdna4_get_pixel_clock_limits;

		case B_DPMS_CAPABILITIES:
			return (void*)rdna4_dpms_capabilities;
		case B_DPMS_MODE:
			return (void*)rdna4_dpms_mode;
		case B_SET_DPMS_MODE:
			return (void*)rdna4_set_dpms_mode;

		case B_ACCELERANT_ENGINE_COUNT:
			return (void*)rdna4_accelerant_engine_count;
		case B_ACQUIRE_ENGINE:
			return (void*)rdna4_acquire_engine;
		case B_RELEASE_ENGINE:
			return (void*)rdna4_release_engine;

		case B_FILL_RECTANGLE:
			return (void*)rdna4_fill_rectangle;
		case B_SCREEN_TO_SCREEN_BLIT:
			return (void*)rdna4_screen_to_screen_blit;
	}
	return NULL;
}
