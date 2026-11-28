/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VRAM_RANGE_MAP_H
#define AMDGPU_VRAM_RANGE_MAP_H

#include <SupportDefs.h>

/*
 * Metadata-only VRAM offset map. Offsets are relative to the VRAM allocation
 * domain, not CPU physical addresses, BAR offsets, or GPU MC addresses.
 * Storage is supplied by the caller; this code never allocates memory.
 */
enum {
	AMDGPU_VRAM_RANGE_FREE = 0,
	AMDGPU_VRAM_RANGE_RESERVED = 1,
	AMDGPU_VRAM_RANGE_ALLOCATED = 2
};

struct amdgpu_vram_range {
	uint64 offset;
	uint64 size;
	uint32 generation;
	uint32 state;
};

struct amdgpu_vram_range_map {
	uint64 capacity;
	amdgpu_vram_range* ranges;
	uint32 range_count;
};

struct amdgpu_vram_range_handle {
	uint32 slot;
	uint32 generation;
};

status_t amdgpu_vram_range_map_init(amdgpu_vram_range_map* map,
	uint64 capacity, amdgpu_vram_range* storage, uint32 storage_count);

status_t amdgpu_vram_reserve_range(amdgpu_vram_range_map* map,
	uint64 offset, uint64 size);

/* First-fit allocation. Alignment must be a nonzero power of two. */
status_t amdgpu_vram_allocate_range(amdgpu_vram_range_map* map,
	uint64 size, uint64 alignment, amdgpu_vram_range_handle* handle,
	uint64* offset);

/* Only live ALLOCATED handles can be released; reserved ranges are permanent. */
status_t amdgpu_vram_release_range(amdgpu_vram_range_map* map,
	amdgpu_vram_range_handle handle);

#endif
