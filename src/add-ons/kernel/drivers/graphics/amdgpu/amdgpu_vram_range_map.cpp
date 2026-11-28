/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <string.h>

#include "amdgpu_vram_range_map.h"


static bool
IsPowerOfTwo(uint64 value)
{
	return value != 0 && (value & (value - 1)) == 0;
}


static bool
RangeValid(uint64 capacity, uint64 offset, uint64 size)
{
	return size != 0 && offset < capacity && size <= capacity - offset;
}


static bool
Overlaps(uint64 aOffset, uint64 aSize, uint64 bOffset, uint64 bSize)
{
	return aOffset < bOffset + bSize && bOffset < aOffset + aSize;
}


status_t
amdgpu_vram_range_map_init(amdgpu_vram_range_map* map, uint64 capacity,
	amdgpu_vram_range* storage, uint32 storageCount)
{
	if (map == NULL || storage == NULL || capacity == 0 || storageCount == 0)
		return B_BAD_VALUE;

	memset(storage, 0, sizeof(amdgpu_vram_range) * storageCount);
	map->capacity = capacity;
	map->ranges = storage;
	map->range_count = storageCount;
	return B_OK;
}


status_t
amdgpu_vram_reserve_range(amdgpu_vram_range_map* map, uint64 offset,
	uint64 size)
{
	if (map == NULL || map->ranges == NULL
		|| !RangeValid(map->capacity, offset, size))
		return B_BAD_VALUE;

	uint32 freeSlot = map->range_count;
	for (uint32 i = 0; i < map->range_count; i++) {
		const amdgpu_vram_range& range = map->ranges[i];
		if (range.state == AMDGPU_VRAM_RANGE_FREE) {
			if (freeSlot == map->range_count)
				freeSlot = i;
			continue;
		}
		if (Overlaps(offset, size, range.offset, range.size))
			return B_BUSY;
	}
	if (freeSlot == map->range_count)
		return B_NO_MEMORY;

	amdgpu_vram_range& range = map->ranges[freeSlot];
	range.offset = offset;
	range.size = size;
	range.state = AMDGPU_VRAM_RANGE_RESERVED;
	range.generation++;
	if (range.generation == 0)
		range.generation = 1;
	return B_OK;
}


status_t
amdgpu_vram_allocate_range(amdgpu_vram_range_map* map, uint64 size,
	uint64 alignment, amdgpu_vram_range_handle* handle, uint64* offset)
{
	if (map == NULL || map->ranges == NULL || handle == NULL || offset == NULL
		|| size == 0 || !IsPowerOfTwo(alignment))
		return B_BAD_VALUE;

	uint32 freeSlot = map->range_count;
	for (uint32 i = 0; i < map->range_count; i++) {
		if (map->ranges[i].state == AMDGPU_VRAM_RANGE_FREE) {
			freeSlot = i;
			break;
		}
	}
	if (freeSlot == map->range_count)
		return B_NO_MEMORY;

	uint64 candidate = 0;
	for (;;) {
		if (candidate > ~(uint64)0 - (alignment - 1))
			return B_NO_MEMORY;
		candidate = (candidate + alignment - 1) & ~(alignment - 1);
		if (candidate >= map->capacity || size > map->capacity - candidate)
			return B_NO_MEMORY;

		bool collision = false;
		uint64 nextCandidate = candidate;
		for (uint32 i = 0; i < map->range_count; i++) {
			const amdgpu_vram_range& range = map->ranges[i];
			if (range.state == AMDGPU_VRAM_RANGE_FREE
				|| !Overlaps(candidate, size, range.offset, range.size))
				continue;
			collision = true;
			if (range.offset <= candidate)
				nextCandidate = range.offset + range.size;
			else if (range.offset < nextCandidate || nextCandidate == candidate)
				nextCandidate = range.offset;
		}
		if (!collision)
			break;
		if (nextCandidate <= candidate)
			return B_BAD_DATA;
		candidate = nextCandidate;
	}

	amdgpu_vram_range& range = map->ranges[freeSlot];
	range.offset = candidate;
	range.size = size;
	range.state = AMDGPU_VRAM_RANGE_ALLOCATED;
	range.generation++;
	if (range.generation == 0)
		range.generation = 1;
	handle->slot = freeSlot;
	handle->generation = range.generation;
	*offset = candidate;
	return B_OK;
}


status_t
amdgpu_vram_release_range(amdgpu_vram_range_map* map,
	amdgpu_vram_range_handle handle)
{
	if (map == NULL || map->ranges == NULL || handle.slot >= map->range_count)
		return B_BAD_VALUE;

	amdgpu_vram_range& range = map->ranges[handle.slot];
	if (range.state != AMDGPU_VRAM_RANGE_ALLOCATED
		|| range.generation != handle.generation)
		return B_BAD_VALUE;

	range.offset = 0;
	range.size = 0;
	range.state = AMDGPU_VRAM_RANGE_FREE;
	return B_OK;
}
