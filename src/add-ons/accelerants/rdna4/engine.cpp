#include "rdna4.h"
#include "accelerant_protos.h"

static engine_token sEngine = {1, B_2D_ACCELERATION, NULL};

uint32 rdna4_accelerant_engine_count(void) { return 1; }

status_t
rdna4_acquire_engine(uint32, uint32, sync_token*, engine_token** token)
{
	if (token == NULL)
		return B_BAD_VALUE;
	*token = &sEngine;
	return B_OK;
}

status_t
rdna4_release_engine(engine_token*, sync_token*)
{
	return B_OK;
}

status_t
rdna4_fill_rectangle(engine_token*, uint32, fill_rect_params*)
{
	/* GFX12 acceleration is deliberately enabled only after the kernel
	   command processor has completed firmware and VM initialization. */
	return B_NOT_SUPPORTED;
}

status_t
rdna4_screen_to_screen_blit(engine_token*, blit_params*)
{
	return B_NOT_SUPPORTED;
}
