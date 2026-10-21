/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "accelerant_protos.h"
#include "accelerant.h"
#include "Ports.h"

#include <string.h>


// Cursor registers are pipe-local. The current framebuffer/modeset path uses
// clone-style scanout, so the same cursor position and bitmap are programmed
// on each pipe attached to a connected port. Gen12+ is gated in hooks.cpp
// until its cursor plane format and register path are validated.
static uint32
get_cursor_pipe_offsets(uint32* offsets, uint32 maxOffsets)
{
	uint32 count = 0;
	for (uint32 i = 0; i < gInfo->port_count; i++) {
		Port* port = gInfo->ports[i];
		if (port == NULL || !port->IsConnected() || port->GetPipe() == NULL)
			continue;

		uint32 offset;
		switch (port->GetPipe()->Index()) {
			case INTEL_PIPE_A:
				offset = 0;
				break;
			case INTEL_PIPE_B:
				offset = 0x1000;
				break;
			case INTEL_PIPE_C:
				offset = 0x2000;
				break;
			case INTEL_PIPE_D:
				offset = 0xf000;
				break;
			default:
				continue;
		}

		bool duplicate = false;
		for (uint32 j = 0; j < count; j++) {
			if (offsets[j] == offset) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate && count < maxOffsets)
			offsets[count++] = offset;
	}

	// Before connector routing is available, preserve the legacy pipe-A
	// behavior rather than leaving register selection undefined.
	if (count == 0 && maxOffsets > 0)
		offsets[count++] = 0;
	return count;
}


status_t
intel_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (width == 0 || height == 0 || width > 64 || height > 64
		|| hotX >= width || hotY >= height
		|| andMask == NULL || xorMask == NULL
		|| gInfo->shared_info->cursor_memory == NULL) {
		return B_BAD_VALUE;
	}

	uint32 pipeOffsets[4];
	uint32 pipeCount = get_cursor_pipe_offsets(pipeOffsets, 4);
	for (uint32 i = 0; i < pipeCount; i++)
		write32(INTEL_CURSOR_CONTROL + pipeOffsets[i], 0);
		// disable cursors before changing shape memory or registers

	// In two-color mode, the data is ordered as follows (always 64 bit per
	// line):
	//	plane 1: line 0 (AND mask)
	//	plane 0: line 0 (XOR mask)
	//	plane 1: line 1 (AND mask)
	//	...
	//
	// If the planes add to the value 0x2, the corresponding pixel is
	// transparent, for 0x3 it inverts the background, so only the first
	// two palette entries will be used (since we're using the 2 color mode).

	uint8* data = gInfo->shared_info->cursor_memory;
	uint8 byteWidth = (width + 7) / 8;

	// The hardware uses a fixed 64-pixel row stride. Initialize every row
	// transparent so shrinking/changing a cursor cannot expose stale mask bits.
	memset(data, 0, 64 * 16);
	for (int32 y = 0; y < 64; y++)
		memset(data + 16 * y, 0xff, 8);

	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < byteWidth; x++) {
			data[16 * y + x] = andMask[byteWidth * y + x];
			data[16 * y + x + 8] = xorMask[byteWidth * y + x];
		}
	}

	// Set palette, size, base, and control on each active pipe.
	gInfo->shared_info->cursor_format = CURSOR_FORMAT_2_COLORS;
	for (uint32 i = 0; i < pipeCount; i++) {
		uint32 offset = pipeOffsets[i];
		write32(INTEL_CURSOR_PALETTE + offset + 0, 0x00ffffff);
		write32(INTEL_CURSOR_PALETTE + offset + 4, 0);
		write32(INTEL_CURSOR_SIZE + offset, height << 12 | width);
		write32(INTEL_CURSOR_BASE + offset,
			(uint32)gInfo->shared_info->physical_graphics_memory
				+ gInfo->shared_info->cursor_buffer_offset);

		// Shape changes must preserve visibility: a hidden cursor stays hidden.
		write32(INTEL_CURSOR_CONTROL + offset,
			(gInfo->shared_info->cursor_visible ? CURSOR_ENABLED : 0)
				| gInfo->shared_info->cursor_format);
	}

	// changing the hot point changes the cursor position, too

	if (hotX != gInfo->shared_info->cursor_hot_x
		|| hotY != gInfo->shared_info->cursor_hot_y) {
		int32 x = read32(INTEL_CURSOR_POSITION);
		int32 y = x >> 16;
		x &= 0xffff;
		
		if (x & CURSOR_POSITION_NEGATIVE)
			x = -(x & CURSOR_POSITION_MASK);
		if (y & CURSOR_POSITION_NEGATIVE)
			y = -(y & CURSOR_POSITION_MASK);

		x += gInfo->shared_info->cursor_hot_x;
		y += gInfo->shared_info->cursor_hot_y;

		gInfo->shared_info->cursor_hot_x = hotX;
		gInfo->shared_info->cursor_hot_y = hotY;

		move_cursor(x, y);
	}

	return B_OK;
}


static void
move_cursor(int32 x, int32 y)
{
	x -= gInfo->shared_info->cursor_hot_x;
	y -= gInfo->shared_info->cursor_hot_y;

	// Position fields are 14-bit magnitudes plus a sign bit. Clamp rather
	// than allowing out-of-range coordinates to overwrite reserved bits.
	if (x < 0) {
		if (x < -(int32)CURSOR_POSITION_MASK)
			x = -(int32)CURSOR_POSITION_MASK;
		x = -x | CURSOR_POSITION_NEGATIVE;
	} else if (x > (int32)CURSOR_POSITION_MASK) {
		x = CURSOR_POSITION_MASK;
	}
	if (y < 0) {
		if (y < -(int32)CURSOR_POSITION_MASK)
			y = -(int32)CURSOR_POSITION_MASK;
		y = -y | CURSOR_POSITION_NEGATIVE;
	} else if (y > (int32)CURSOR_POSITION_MASK) {
		y = CURSOR_POSITION_MASK;
	}

	// Use unsigned fields for the packed register value; shifting a signed
	// coordinate with its sign bit set can overflow signed int32.
	uint32 encodedX = (uint32)x;
	uint32 encodedY = (uint32)y;
	uint32 pipeOffsets[4];
	uint32 pipeCount = get_cursor_pipe_offsets(pipeOffsets, 4);
	for (uint32 i = 0; i < pipeCount; i++) {
		write32(INTEL_CURSOR_POSITION + pipeOffsets[i],
			(encodedY << 16) | encodedX);
	}
}


void
intel_move_cursor(uint16 _x, uint16 _y)
{
	move_cursor((int32)_x, (int32)_y);
}


void
intel_show_cursor(bool isVisible)
{
	uint32 pipeOffsets[4];
	uint32 pipeCount = get_cursor_pipe_offsets(pipeOffsets, 4);
	if (gInfo->shared_info->cursor_visible == isVisible) {
		// Explicit hide requests also clear cursors that may have been left
		// enabled by firmware before the accelerant initialized.
		if (!isVisible) {
			for (uint32 i = 0; i < pipeCount; i++)
				write32(INTEL_CURSOR_CONTROL + pipeOffsets[i], 0);
		}
		return;
	}

	for (uint32 i = 0; i < pipeCount; i++) {
		uint32 offset = pipeOffsets[i];
		write32(INTEL_CURSOR_CONTROL + offset,
			(isVisible ? CURSOR_ENABLED : 0) | gInfo->shared_info->cursor_format);
		write32(INTEL_CURSOR_BASE + offset,
			(uint32)gInfo->shared_info->physical_graphics_memory
				+ gInfo->shared_info->cursor_buffer_offset);
	}

	gInfo->shared_info->cursor_visible = isVisible;
}

