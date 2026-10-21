/*
 * Copyright 2026, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * CPU-only 2D rendering backend for intel_extreme.
 *
 * This backend deliberately submits no GPU commands. It writes to the
 * currently mapped scanout buffer shared with the kernel driver. The
 * modesetting, connector, EDID, DPMS, and framebuffer allocation paths remain
 * owned by the existing accelerant.
 */

#include <string.h>

#include "accelerant.h"
#include "accelerant_protos.h"


namespace {

struct cpu_framebuffer {
	uint8* base;
	uint32 width;
	uint32 height;
	uint32 stride;
	uint32 bytesPerPixel;
	uint32 colorSpace;
	size_t size;
};


static bool
get_framebuffer(cpu_framebuffer& fb)
{
	if (gInfo == NULL || gInfo->shared_info == NULL)
		return false;

	intel_shared_info& info = *gInfo->shared_info;
	if (info.graphics_memory == NULL || info.bytes_per_row == 0
		|| info.current_mode.virtual_width == 0
		|| info.current_mode.virtual_height == 0)
		return false;

	uint32 bytesPerPixel;
	switch (info.current_mode.space) {
		case B_CMAP8:
			bytesPerPixel = 1;
			break;
		case B_RGB15_LITTLE:
		case B_RGB16_LITTLE:
			bytesPerPixel = 2;
			break;
		case B_RGB32_LITTLE:
			bytesPerPixel = 4;
			break;
		default:
			return false;
	}

	const uint32 offset = info.frame_buffer_offset;
	const uint32 width = info.current_mode.virtual_width;
	const uint32 height = info.current_mode.virtual_height;
	const uint32 stride = info.bytes_per_row;

	// Reject corrupt modes and offsets before any pointer arithmetic. Keep all
	// multiplications in size_t after checking the 32-bit stride constraints.
	if (width > stride / bytesPerPixel
		|| width > 0x7fffffffU || height > 0x7fffffffU
		|| (size_t)height > (size_t)-1 / stride)
		return false;

	const size_t size = (size_t)stride * height;
	if (offset > info.graphics_memory_size
		|| size > info.graphics_memory_size - offset)
		return false;

	fb.base = info.graphics_memory + offset;
	fb.width = width;
	fb.height = height;
	fb.stride = stride;
	fb.bytesPerPixel = bytesPerPixel;
	fb.colorSpace = info.current_mode.space;
	fb.size = size;
	return true;
}


static bool
clip_rectangle(const cpu_framebuffer& fb, int32& left, int32& top,
	int32& right, int32& bottom)
{
	// Accelerant rectangle hooks use inclusive right/bottom coordinates.
	if (right < left || bottom < top || right < 0 || bottom < 0
		|| left >= (int32)fb.width || top >= (int32)fb.height)
		return false;

	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right >= (int32)fb.width)
		right = (int32)fb.width - 1;
	if (bottom >= (int32)fb.height)
		bottom = (int32)fb.height - 1;
	return right >= left && bottom >= top;
}


static uint16
to_rgb16(uint32 color, bool rgb15)
{
	uint32 red = (color >> 16) & 0xff;
	uint32 green = (color >> 8) & 0xff;
	uint32 blue = color & 0xff;

	if (rgb15)
		return (uint16)(((red >> 3) << 10) | ((green >> 3) << 5)
			| (blue >> 3));
	return (uint16)(((red >> 3) << 11) | ((green >> 2) << 5)
		| (blue >> 3));
}


static void
store_pixel(const cpu_framebuffer& fb, uint8* pixel, uint32 color)
{
	switch (fb.colorSpace) {
		case B_CMAP8:
			*pixel = (uint8)color;
			break;
		case B_RGB15_LITTLE:
		case B_RGB16_LITTLE: {
			uint16 value = to_rgb16(color,
				fb.colorSpace == B_RGB15_LITTLE);
			memcpy(pixel, &value, sizeof(value));
			break;
		}
		case B_RGB32_LITTLE:
			// B_RGB32_LITTLE stores the low 24-bit RGB value. Copy rather than
			// cast to avoid alignment assumptions for framebuffer rows.
			color &= 0x00ffffff;
			memcpy(pixel, &color, sizeof(color));
			break;
	}
}


static void
fill_rect(cpu_framebuffer& fb, int32 left, int32 top, int32 right,
	int32 bottom, uint32 color, bool invert)
{
	if (!clip_rectangle(fb, left, top, right, bottom))
		return;

	for (int32 y = top; y <= bottom; y++) {
		uint8* pixel = fb.base + (size_t)y * fb.stride
			+ (size_t)left * fb.bytesPerPixel;
		for (int32 x = left; x <= right; x++, pixel += fb.bytesPerPixel) {
			if (invert) {
				for (uint32 byte = 0; byte < fb.bytesPerPixel; byte++)
					pixel[byte] ^= 0xff;
			} else
				store_pixel(fb, pixel, color);
		}
	}
}

} // namespace


uint32
intel_cpu_accelerant_engine_count(void)
{
	return 1;
}


status_t
intel_cpu_acquire_engine(uint32 capabilities, uint32 maxWait,
	sync_token* syncToken, engine_token** _engineToken)
{
	if (_engineToken == NULL)
		return B_BAD_VALUE;
	*_engineToken = NULL;
	if (gInfo == NULL || gInfo->shared_info == NULL)
		return B_NO_INIT;

	(void)capabilities;
	(void)maxWait;
	status_t status = acquire_lock(&gInfo->shared_info->engine_lock);
	if (status != B_OK)
		return status;

	static engine_token sCPUEngineToken = {1, B_2D_ACCELERATION, NULL};
	*_engineToken = &sCPUEngineToken;
	if (syncToken != NULL)
		intel_cpu_sync_to_token(syncToken);
	return B_OK;
}


status_t
intel_cpu_release_engine(engine_token* engineToken, sync_token* syncToken)
{
	if (engineToken == NULL)
		return B_BAD_VALUE;
	if (gInfo == NULL || gInfo->shared_info == NULL)
		return B_NO_INIT;
	if (syncToken != NULL) {
		syncToken->engine_id = engineToken->engine_id;
		syncToken->counter = 0;
	}
	release_lock(&gInfo->shared_info->engine_lock);
	return B_OK;
}


void
intel_cpu_wait_engine_idle(void)
{
	// Rendering is synchronous CPU memory access; no GPU ring or flush is
	// required. The engine lock serializes cooperating accelerant clients.
}


status_t
intel_cpu_get_sync_token(engine_token* engineToken, sync_token* syncToken)
{
	if (engineToken == NULL || syncToken == NULL)
		return B_BAD_VALUE;
	syncToken->engine_id = engineToken->engine_id;
	syncToken->counter = 0;
	return B_OK;
}


status_t
intel_cpu_sync_to_token(sync_token* syncToken)
{
	// All operations in this backend are synchronous and write directly to
	// mapped scanout memory; there is no deferred work to wait for.
	if (gInfo == NULL || gInfo->shared_info == NULL)
		return B_NO_INIT;
	(void)syncToken;
	return B_OK;
}


void
intel_cpu_screen_to_screen_blit(engine_token* token, blit_params* params,
	uint32 count)
{
	cpu_framebuffer fb;
	if (params == NULL || !get_framebuffer(fb))
		return;

	for (uint32 i = 0; i < count; i++) {
		int32 sourceLeft = params[i].src_left;
		int32 sourceTop = params[i].src_top;
		int32 destLeft = params[i].dest_left;
		int32 destTop = params[i].dest_top;
		int32 width = (int32)params[i].width + 1;
		int32 height = (int32)params[i].height + 1;
		if (width <= 0 || height <= 0)
			continue;

		// Clip source and destination together, preserving their offset.
		if (sourceLeft < 0) { int32 d = -sourceLeft; sourceLeft = 0; destLeft += d; width -= d; }
		if (sourceTop < 0) { int32 d = -sourceTop; sourceTop = 0; destTop += d; height -= d; }
		if (destLeft < 0) { int32 d = -destLeft; destLeft = 0; sourceLeft += d; width -= d; }
		if (destTop < 0) { int32 d = -destTop; destTop = 0; sourceTop += d; height -= d; }
		if (sourceLeft >= (int32)fb.width || destLeft >= (int32)fb.width
			|| sourceTop >= (int32)fb.height || destTop >= (int32)fb.height)
			continue;
		if (width > (int32)fb.width - sourceLeft) width = (int32)fb.width - sourceLeft;
		if (width > (int32)fb.width - destLeft) width = (int32)fb.width - destLeft;
		if (height > (int32)fb.height - sourceTop) height = (int32)fb.height - sourceTop;
		if (height > (int32)fb.height - destTop) height = (int32)fb.height - destTop;
		if (width <= 0 || height <= 0)
			continue;

		size_t rowBytes = (size_t)width * fb.bytesPerPixel;
		uint8* source = fb.base + (size_t)sourceTop * fb.stride
			+ (size_t)sourceLeft * fb.bytesPerPixel;
		uint8* destination = fb.base + (size_t)destTop * fb.stride
			+ (size_t)destLeft * fb.bytesPerPixel;

		// memmove per row in the correct vertical direction handles overlapping
		// rectangles without a temporary allocation.
		if (destTop > sourceTop) {
			for (int32 row = height - 1; row >= 0; row--)
				memmove(destination + (size_t)row * fb.stride,
					source + (size_t)row * fb.stride, rowBytes);
		} else {
			for (int32 row = 0; row < height; row++)
				memmove(destination + (size_t)row * fb.stride,
					source + (size_t)row * fb.stride, rowBytes);
		}
	}
}


void
intel_cpu_fill_rectangle(engine_token* token, uint32 color,
	fill_rect_params* params, uint32 count)
{
	cpu_framebuffer fb;
	if (params == NULL || !get_framebuffer(fb))
		return;
	for (uint32 i = 0; i < count; i++)
		fill_rect(fb, params[i].left, params[i].top, params[i].right,
			params[i].bottom, color, false);
}


void
intel_cpu_invert_rectangle(engine_token* token, fill_rect_params* params,
	uint32 count)
{
	cpu_framebuffer fb;
	if (params == NULL || !get_framebuffer(fb))
		return;
	for (uint32 i = 0; i < count; i++)
		fill_rect(fb, params[i].left, params[i].top, params[i].right,
			params[i].bottom, 0, true);
}


void
intel_cpu_fill_span(engine_token* token, uint32 color, uint16* _params,
	uint32 count)
{
	struct span_params {
		uint16 top;
		uint16 left;
		uint16 right;
	};
	cpu_framebuffer fb;
	if (_params == NULL || !get_framebuffer(fb))
		return;

	span_params* params = (span_params*)_params;
	for (uint32 i = 0; i < count; i++)
		// The span hook uses an exclusive right edge, unlike fill rectangles.
		fill_rect(fb, params[i].left, params[i].top,
			(int32)params[i].right - 1, params[i].top, color, false);
}
