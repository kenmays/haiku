#ifndef AMDGPU_RDNA4_ACCELERANT_PROTOS_H
#define AMDGPU_RDNA4_ACCELERANT_PROTOS_H

#include <Accelerant.h>
#include <SupportDefs.h>

#ifdef __cplusplus
extern "C" {
#endif

status_t amdgpu_rdna4_init_accelerant(int fd);
ssize_t amdgpu_rdna4_accelerant_clone_info_size(void);
void amdgpu_rdna4_get_accelerant_clone_info(void* data);
status_t amdgpu_rdna4_clone_accelerant(void* data);
void amdgpu_rdna4_uninit_accelerant(void);
status_t amdgpu_rdna4_get_device_info(accelerant_device_info* info);

uint32 amdgpu_rdna4_mode_count(void);
status_t amdgpu_rdna4_get_mode_list(display_mode* modes);
status_t amdgpu_rdna4_get_preferred_mode(display_mode* mode);
status_t amdgpu_rdna4_get_display_mode(display_mode* mode);
status_t amdgpu_rdna4_set_display_mode(display_mode* mode);
status_t amdgpu_rdna4_get_frame_buffer_config(frame_buffer_config* config);
status_t amdgpu_rdna4_get_pixel_clock_limits(display_mode* mode,
	uint32* low, uint32* high);
status_t amdgpu_rdna4_get_edid_info(void* info, size_t size,
	uint32* edid_version);

uint32 amdgpu_rdna4_dpms_capabilities(void);
uint32 amdgpu_rdna4_dpms_mode(void);
status_t amdgpu_rdna4_set_dpms_mode(uint32 mode);

#ifdef __cplusplus
}
#endif

#endif