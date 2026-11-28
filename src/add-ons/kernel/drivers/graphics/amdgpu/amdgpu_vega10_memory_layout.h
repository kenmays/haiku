/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VEGA10_MEMORY_LAYOUT_H
#define AMDGPU_VEGA10_MEMORY_LAYOUT_H

#include <SupportDefs.h>

/* Vega 10 MMHUB framebuffer location registers encode 16 MiB units. */
static const uint64 AMDGPU_VEGA10_FB_LOCATION_GRANULARITY = 1ULL << 24;
static const uint64 AMDGPU_VEGA10_BYTES_PER_MIB = 1024ULL * 1024ULL;

struct amdgpu_vega10_memory_layout_input {
	/* NBIO RCC_CONFIG_MEMSIZE value, in MiB. */
	uint32 vram_size_mib;

	/* Decoded MMHUB MC_VM_FB_LOCATION_BASE/TOP, inclusive byte addresses. */
	uint64 fb_start;
	uint64 fb_end;

	/* Decoded GC MC_VM_FB_OFFSET, in bytes. */
	uint64 mc_fb_offset;

	/* PCI BAR0 CPU physical aperture, not total VRAM capacity. */
	uint64 bar0_physical_base;
	uint64 bar0_aperture_size;
};

struct amdgpu_vega10_memory_layout {
	uint64 vram_size;
	uint64 fb_start;
	uint64 fb_end;
	uint64 fb_range_size;
	uint64 mc_fb_offset;
	uint64 bar0_physical_base;
	uint64 bar0_aperture_size;
	uint64 visible_vram_size;
};

/*
 * Build a validated metadata snapshot. No registers are read or written;
 * no BAR is mapped and no VRAM is allocated. The caller must supply values
 * obtained from the correct Vega 10 IP blocks after safe MMIO access exists.
 */
status_t amdgpu_build_vega10_memory_layout(
	const amdgpu_vega10_memory_layout_input* input,
	amdgpu_vega10_memory_layout* layout);

#endif
