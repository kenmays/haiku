#include "accelerant.h"
#include "accelerant_protos.h"

extern "C" void*
get_accelerant_hook(uint32 feature, void* data)
{
	(void)data;
	switch (feature) {
		case B_INIT_ACCELERANT:
			return (void*)amdgpu_rdna4_init_accelerant;
		case B_UNINIT_ACCELERANT:
			return (void*)amdgpu_rdna4_uninit_accelerant;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return (void*)amdgpu_rdna4_get_device_info;
		case B_DPMS_CAPABILITIES:
			return (void*)amdgpu_rdna4_dpms_capabilities;
		case B_DPMS_MODE:
			return (void*)amdgpu_rdna4_dpms_mode;
		case B_SET_DPMS_MODE:
			return (void*)amdgpu_rdna4_set_dpms_mode;
		case B_ACCELERANT_MODE_COUNT:
			return (void*)amdgpu_rdna4_mode_count;
		case B_GET_MODE_LIST:
			return (void*)amdgpu_rdna4_get_mode_list;
		case B_SET_DISPLAY_MODE:
			return (void*)amdgpu_rdna4_set_display_mode;
		case B_GET_DISPLAY_MODE:
			return (void*)amdgpu_rdna4_get_display_mode;
	}
	return NULL;
}
