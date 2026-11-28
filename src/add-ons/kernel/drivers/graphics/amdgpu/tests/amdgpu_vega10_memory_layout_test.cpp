/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_vega10_memory_layout.h"

#include <stdio.h>
#include <string.h>


static bool
Check(bool condition, const char* name)
{
	if (!condition)
		fprintf(stderr, "FAIL: %s\n", name);
	return condition;
}


int
main()
{
	bool success = true;
	amdgpu_vega10_memory_layout_input input = {};
	input.vram_size_mib = 8192;
	input.fb_start = 0x80000000;
	input.fb_end = 0x27fffffff;
	input.mc_fb_offset = 0x80000000;
	input.bar0_physical_base = 0xE0000000;
	input.bar0_aperture_size = 256ULL * AMDGPU_VEGA10_BYTES_PER_MIB;

	amdgpu_vega10_memory_layout layout;
	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout)
		== B_OK, "accepts valid 8 GiB VRAM with a 256 MiB visible BAR aperture");
	success &= Check(layout.vram_size == 8192ULL * AMDGPU_VEGA10_BYTES_PER_MIB
		&& layout.visible_vram_size == input.bar0_aperture_size
		&& layout.fb_range_size >= layout.vram_size,
		"keeps VRAM capacity, MC range, and CPU-visible aperture distinct");

	input.bar0_aperture_size = 16ULL * 1024 * AMDGPU_VEGA10_BYTES_PER_MIB;
	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout) == B_OK
		&& layout.visible_vram_size == layout.vram_size,
		"caps visible aperture at total VRAM capacity");

	input.vram_size_mib = 0;
	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout)
		== B_BAD_VALUE, "rejects zero VRAM capacity");
	input.vram_size_mib = 8192;

	input.fb_start++;
	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout)
		== B_BAD_DATA, "rejects a misaligned framebuffer base");
	input.fb_start--;

	input.fb_end--;
	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout)
		== B_BAD_DATA, "rejects a framebuffer top not ending on a 16 MiB boundary");
	input.fb_end++;

	input.mc_fb_offset++;
	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout)
		== B_BAD_DATA, "rejects a misaligned MC framebuffer offset");
	input.mc_fb_offset--;

	input.fb_end = input.fb_start + 1024ULL * AMDGPU_VEGA10_BYTES_PER_MIB - 1;
	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout)
		== B_BAD_DATA, "rejects an MC framebuffer range smaller than VRAM");
	input.fb_end = 0x27fffffff;

	input.bar0_physical_base = 0;
	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout)
		== B_BAD_VALUE, "rejects an absent BAR0 physical base");
	input.bar0_physical_base = 0xE0000000;

	input.bar0_physical_base = ~(uint64)0 - 0x7f;
	input.bar0_aperture_size = 0x1000;
	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout)
		== B_BAD_VALUE, "rejects a BAR0 physical range that wraps");
	input.bar0_physical_base = 0xE0000000;
	input.bar0_aperture_size = 0;

	success &= Check(amdgpu_build_vega10_memory_layout(&input, &layout)
		== B_BAD_VALUE, "rejects an empty BAR0 aperture");

	if (!success)
		return 1;
	printf("AMDGPU Vega 10 memory layout tests passed.\n");
	return 0;
}
