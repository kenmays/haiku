/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <string.h>

#include "amdgpu_vega10_memory_layout.h"


status_t
amdgpu_build_vega10_memory_layout(
	const amdgpu_vega10_memory_layout_input* input,
	amdgpu_vega10_memory_layout* layout)
{
	if (input == NULL || layout == NULL)
		return B_BAD_VALUE;

	memset(layout, 0, sizeof(*layout));

	if (input->vram_size_mib == 0
		|| input->bar0_physical_base == 0
		|| input->bar0_aperture_size == 0)
		return B_BAD_VALUE;

	const uint64 maxValue = ~(uint64)0;
	if ((uint64)input->vram_size_mib
		> maxValue / AMDGPU_VEGA10_BYTES_PER_MIB)
		return B_BAD_VALUE;

	const uint64 vramSize = (uint64)input->vram_size_mib
		* AMDGPU_VEGA10_BYTES_PER_MIB;

	if (input->fb_start > input->fb_end
		|| (input->fb_start & (AMDGPU_VEGA10_FB_LOCATION_GRANULARITY - 1)) != 0
		|| (input->fb_end & (AMDGPU_VEGA10_FB_LOCATION_GRANULARITY - 1))
			!= AMDGPU_VEGA10_FB_LOCATION_GRANULARITY - 1
		|| (input->mc_fb_offset
			& (AMDGPU_VEGA10_FB_LOCATION_GRANULARITY - 1)) != 0)
		return B_BAD_DATA;

	const uint64 fbDelta = input->fb_end - input->fb_start;
	if (fbDelta == maxValue)
		return B_BAD_DATA;
	const uint64 fbRangeSize = fbDelta + 1;
	if (fbRangeSize < vramSize)
		return B_BAD_DATA;

	if (input->bar0_aperture_size - 1
		> maxValue - input->bar0_physical_base)
		return B_BAD_VALUE;

	layout->vram_size = vramSize;
	layout->fb_start = input->fb_start;
	layout->fb_end = input->fb_end;
	layout->fb_range_size = fbRangeSize;
	layout->mc_fb_offset = input->mc_fb_offset;
	layout->bar0_physical_base = input->bar0_physical_base;
	layout->bar0_aperture_size = input->bar0_aperture_size;
	layout->visible_vram_size = input->bar0_aperture_size < vramSize
		? input->bar0_aperture_size : vramSize;
	return B_OK;
}
