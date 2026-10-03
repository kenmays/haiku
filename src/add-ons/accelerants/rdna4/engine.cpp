#include "rdna4.h"
#include "accelerant_protos.h"

#include <string.h>

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

void
rdna4_wait_engine_idle(void)
{
}

status_t
rdna4_get_sync_token(engine_token*, sync_token* token)
{
	if (token == NULL)
		return B_BAD_VALUE;
	memset(token, 0, sizeof(*token));
	return B_OK;
}

status_t
rdna4_sync_to_token(sync_token*)
{
	return B_OK;
}

void
rdna4_fill_rectangle(engine_token*, uint32 color,
	fill_rect_params* list, uint32 count)
{
	if (list == NULL || sShared == NULL || sFramebuffer == NULL)
		return;

	const uint32 width = sShared->current_mode.virtual_width;
	const uint32 height = sShared->current_mode.virtual_height;
	const uint32 stride = sShared->bytes_per_row;
	if (sShared->bits_per_pixel != 32)
		return;

	while (count-- != 0) {
		int left = list->left;
		int top = list->top;
		int right = list->right;
		int bottom = list->bottom;
		if (left < 0) left = 0;
		if (top < 0) top = 0;
		if (right >= (int)width) right = width - 1;
		if (bottom >= (int)height) bottom = height - 1;
		if (left <= right && top <= bottom) {
			for (int y = top; y <= bottom; y++) {
				uint32* row = (uint32*)(sFramebuffer + y * stride
					+ left * 4);
				for (int x = left; x <= right; x++)
					row[x - left] = color;
			}
		}
		list++;
	}
}

void
rdna4_screen_to_screen_blit(engine_token*, blit_params* list, uint32 count)
{
	if (list == NULL || sShared == NULL || sFramebuffer == NULL)
		return;

	const uint32 width = sShared->current_mode.virtual_width;
	const uint32 height = sShared->current_mode.virtual_height;
	const uint32 stride = sShared->bytes_per_row;
	if (sShared->bits_per_pixel != 32)
		return;

	while (count-- != 0) {
		int sx = list->src_left;
		int sy = list->src_top;
		int dx = list->dest_left;
		int dy = list->dest_top;
		int w = list->width + 1;
		int h = list->height + 1;
		if (w <= 0 || h <= 0) {
			list++;
			continue;
		}

		if (sx < 0) { dx -= sx; w += sx; sx = 0; }
		if (sy < 0) { dy -= sy; h += sy; sy = 0; }
		if (dx < 0) { sx -= dx; w += dx; dx = 0; }
		if (dy < 0) { sy -= dy; h += dy; dy = 0; }
		if (sx + w > (int)width) w = width - sx;
		if (dx + w > (int)width) w = width - dx;
		if (sy + h > (int)height) h = height - sy;
		if (dy + h > (int)height) h = height - dy;

		if (w > 0 && h > 0) {
			if (dy > sy) {
				for (int y = h - 1; y >= 0; y--)
					memmove(sFramebuffer + (dy + y) * stride + dx * 4,
						sFramebuffer + (sy + y) * stride + sx * 4,
						w * 4);
			} else {
				for (int y = 0; y < h; y++)
					memmove(sFramebuffer + (dy + y) * stride + dx * 4,
						sFramebuffer + (sy + y) * stride + sx * 4,
						w * 4);
			}
		}
		list++;
	}
}
