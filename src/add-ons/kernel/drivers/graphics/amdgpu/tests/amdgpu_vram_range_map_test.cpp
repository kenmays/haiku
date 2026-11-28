/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_vram_range_map.h"

#include <stdio.h>


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
	amdgpu_vram_range storage[8];
	amdgpu_vram_range_map map;
	success &= Check(amdgpu_vram_range_map_init(&map, 1024 * 1024,
		storage, 8) == B_OK, "initializes caller-owned map storage");

	success &= Check(amdgpu_vram_reserve_range(&map, 0, 64 * 1024) == B_OK,
		"reserves low firmware region");
	success &= Check(amdgpu_vram_reserve_range(&map, 512 * 1024,
		64 * 1024) == B_OK, "reserves a framebuffer region");
	success &= Check(amdgpu_vram_reserve_range(&map, 32 * 1024,
		4096) == B_BUSY, "rejects overlap with firmware reservation");
	success &= Check(amdgpu_vram_reserve_range(&map, 1024 * 1024 - 1,
		2) == B_BAD_VALUE, "rejects reservation outside capacity");

	amdgpu_vram_range_handle first;
	uint64 firstOffset = 0;
	success &= Check(amdgpu_vram_allocate_range(&map, 8192, 4096,
		&first, &firstOffset) == B_OK && firstOffset == 64 * 1024,
		"first-fit allocation skips reserved firmware range");
	amdgpu_vram_range_handle second;
	uint64 secondOffset = 0;
	success &= Check(amdgpu_vram_allocate_range(&map, 8192, 4096,
		&second, &secondOffset) == B_OK
		&& secondOffset == firstOffset + 8192,
		"allocates adjacent non-overlapping range");

	success &= Check(amdgpu_vram_release_range(&map, first) == B_OK,
		"releases a live allocation");
	success &= Check(amdgpu_vram_release_range(&map, first) == B_BAD_VALUE,
		"rejects double release");
	amdgpu_vram_range_handle reused;
	uint64 reusedOffset = 0;
	success &= Check(amdgpu_vram_allocate_range(&map, 4096, 4096,
		&reused, &reusedOffset) == B_OK && reusedOffset == firstOffset
		&& (reused.generation != first.generation
			|| reused.slot != first.slot),
		"reuses released space without accepting stale handle");

	amdgpu_vram_range_handle invalid;
	uint64 invalidOffset;
	success &= Check(amdgpu_vram_allocate_range(&map, 4096, 3,
		&invalid, &invalidOffset) == B_BAD_VALUE,
		"rejects non-power-of-two alignment");
	success &= Check(amdgpu_vram_allocate_range(&map, 2 * 1024 * 1024,
		4096, &invalid, &invalidOffset) == B_NO_MEMORY,
		"rejects allocation larger than capacity");

	if (!success)
		return 1;
	printf("AMDGPU VRAM range map tests passed.\n");
	return 0;
}
