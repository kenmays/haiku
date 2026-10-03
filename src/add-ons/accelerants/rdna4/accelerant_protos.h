#ifndef RDNA4_ACCELERANT_PROTOS_H
#define RDNA4_ACCELERANT_PROTOS_H

#include <Accelerant.h>

#ifdef __cplusplus
extern "C" {
#endif

status_t rdna4_init_accelerant(int fd);
void rdna4_uninit_accelerant(void);
status_t rdna4_get_accelerant_device_info(accelerant_device_info* info);

uint32 rdna4_accelerant_mode_count(void);
status_t rdna4_get_mode_list(display_mode* list);
status_t rdna4_propose_display_mode(display_mode* target,
	const display_mode* low, const display_mode* high);
status_t rdna4_set_display_mode(display_mode* mode);
status_t rdna4_get_display_mode(display_mode* mode);
status_t rdna4_get_preferred_display_mode(display_mode* mode);
status_t rdna4_get_frame_buffer_config(frame_buffer_config* config);
status_t rdna4_get_pixel_clock_limits(display_mode* mode, uint32* low,
	uint32* high);

uint32 rdna4_dpms_capabilities(void);
uint32 rdna4_dpms_mode(void);
status_t rdna4_set_dpms_mode(uint32 mode);

uint32 rdna4_accelerant_engine_count(void);
status_t rdna4_acquire_engine(uint32 capabilities, uint32 maxWait,
	sync_token* syncToken, engine_token** engineToken);
status_t rdna4_release_engine(engine_token* engineToken, sync_token* syncToken);

void rdna4_fill_rectangle(engine_token*, uint32 color,
	fill_rect_params*, uint32 count);
void rdna4_screen_to_screen_blit(engine_token*, blit_params*, uint32 count);
void rdna4_wait_engine_idle(void);
status_t rdna4_get_sync_token(engine_token*, sync_token*);
status_t rdna4_sync_to_token(sync_token*);

#ifdef __cplusplus
}
#endif

#endif
