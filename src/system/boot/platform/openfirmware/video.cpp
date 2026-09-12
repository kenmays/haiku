/*
 * Copyright 2004, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2010 Andreas Färber <andreas.faerber@web.de>
 * Power Mac G5 Open Firmware framebuffer support.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include <boot/platform.h>
#include <boot/stage2.h>
#include <boot/platform/generic/text_console.h>
#include <boot/platform/generic/video.h>
#include <edid.h>
#include <platform/openfirmware/openfirmware.h>

static intptr_t sScreen;

void
platform_blit4(addr_t frameBuffer, const uint8* data,
	uint16 width, uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
	panic("platform_blit4(): not implemented\n");
}

extern "C" void
platform_set_palette(const uint8* palette)
{
	switch (gKernelArgs.frame_buffer.depth) {
		case 8:
			if (of_call_method(sScreen, "set-colors", 3, 0,
					256, 0, palette) == OF_FAILED) {
				for (int index = 0; index < 256; index++) {
					of_call_method(sScreen, "color!", 4, 0, index,
						palette[index * 3 + 2], palette[index * 3 + 1],
						palette[index * 3 + 0]);
				}
			}
			break;
		default:
			break;
	}
}

static bool
read_screen_u32(intptr_t package, const char* property, uint32& value)
{
	uint32 raw;
	if (of_getprop(package, property, &raw, sizeof(raw)) == OF_FAILED)
		return false;
	value = B_BENDIAN_TO_HOST_INT32(raw);
	return true;
}

static bool
read_screen_address(intptr_t package, phys_addr_t& address)
{
	uint32 low;
	if (of_getprop(package, "address", &low, sizeof(low)) != OF_FAILED) {
		address = B_BENDIAN_TO_HOST_INT32(low);
		return true;
	}

	uint32 cells[2];
	if (of_getprop(package, "address", cells, sizeof(cells)) != 8)
		return false;
	address = ((uint64)B_BENDIAN_TO_HOST_INT32(cells[0]) << 32)
		| B_BENDIAN_TO_HOST_INT32(cells[1]);
	return true;
}

extern "C" void
platform_switch_to_logo(void)
{
	if ((platform_boot_options() & BOOT_OPTION_DEBUG_OUTPUT) != 0)
		return;

	sScreen = of_open("screen");
	if (sScreen == OF_FAILED)
		return;

	intptr_t package = of_instance_to_package(sScreen);
	if (package == OF_FAILED)
		return;

	uint32 width, height;
	if (of_call_method(sScreen, "dimensions", 0, 2, &height, &width)
		== OF_FAILED) {
		if (!read_screen_u32(package, "width", width)
			|| !read_screen_u32(package, "height", height))
			return;
	}

	uint32 depth, lineBytes;
	if (!read_screen_u32(package, "depth", depth)
		|| !read_screen_u32(package, "linebytes", lineBytes))
		return;

	phys_addr_t address;
	if (!read_screen_address(package, address))
		return;
	if (width == 0 || height == 0 || lineBytes == 0)
		return;

	gKernelArgs.frame_buffer.physical_buffer.start = address;
	gKernelArgs.frame_buffer.physical_buffer.size = (uint64)lineBytes * height;
	gKernelArgs.frame_buffer.width = width;
	gKernelArgs.frame_buffer.height = height;
	gKernelArgs.frame_buffer.depth = depth;
	gKernelArgs.frame_buffer.bytes_per_row = lineBytes;
	gKernelArgs.frame_buffer.enabled = true;

	console_set_cursor(0, 0);
	dprintf("Open Firmware framebuffer: %" B_PRIu32 "x%" B_PRIu32
		"x%" B_PRIu32 ", stride=%" B_PRIu32 ", phys=%" B_PRIxPHYS "\n",
		gKernelArgs.frame_buffer.width, gKernelArgs.frame_buffer.height,
		gKernelArgs.frame_buffer.depth, gKernelArgs.frame_buffer.bytes_per_row,
		gKernelArgs.frame_buffer.physical_buffer.start);

	video_display_splash(gKernelArgs.frame_buffer.physical_buffer.start);
}

extern "C" void
platform_switch_to_text_mode(void)
{
	/* The G5 display mode is owned by Open Firmware. Keep its current mode. */
}

extern "C" status_t
platform_init_video(void)
{
	gKernelArgs.frame_buffer.enabled = false;

	intptr_t screen = of_finddevice("screen");
	if (screen == OF_FAILED)
		return B_NO_INIT;

	edid1_raw edidRaw;
	if (of_getprop(screen, "EDID", &edidRaw, sizeof(edidRaw)) != OF_FAILED) {
		edid1_info info;
		edid_decode(&info, &edidRaw);
		gKernelArgs.edid_info = kernel_args_malloc(sizeof(edid1_info));
		if (gKernelArgs.edid_info != NULL)
			memcpy(gKernelArgs.edid_info, &info, sizeof(edid1_info));
	}

	return B_OK;
}
