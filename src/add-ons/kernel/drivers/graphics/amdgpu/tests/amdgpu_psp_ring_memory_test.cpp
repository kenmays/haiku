/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_psp_ring_memory.h"

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
	uint8 ring[AMDGPU_PSP_VEGA10_RING_SIZE];
	amdgpu_psp_ring_memory memory;
	memory.gpu_address = 0x100000;
	memory.cpu_address = ring;
	memory.allocation_size = sizeof(ring);
	memory.memory_domain = AMDGPU_PSP_MEMORY_DOMAIN_VRAM;
	memory.capabilities = AMDGPU_PSP_RING_REQUIRED_CAPABILITIES;

	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory) == B_OK,
		"accepts an aligned, CPU-mapped, contiguous, sync-capable VRAM ring");

	success &= Check(amdgpu_psp_validate_vega10_ring_memory(NULL) == B_BAD_VALUE,
		"rejects a null descriptor");

	memory.memory_domain = AMDGPU_PSP_MEMORY_DOMAIN_SYSTEM;
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_NOT_SUPPORTED,
		"rejects system RAM even when its descriptor looks contiguous");
	memory.memory_domain = AMDGPU_PSP_MEMORY_DOMAIN_VRAM;

	memory.capabilities &= ~AMDGPU_PSP_RING_CPU_MAPPED;
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_NOT_SUPPORTED, "requires a CPU mapping for ring-frame writes");
	memory.capabilities = AMDGPU_PSP_RING_REQUIRED_CAPABILITIES;

	memory.capabilities &= ~AMDGPU_PSP_RING_GPU_ADDRESS_VALID;
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_NOT_SUPPORTED, "requires a valid GPU/MC address");
	memory.capabilities = AMDGPU_PSP_RING_REQUIRED_CAPABILITIES;

	memory.capabilities &= ~AMDGPU_PSP_RING_CONTIGUOUS;
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_NOT_SUPPORTED, "requires physically contiguous VRAM backing");
	memory.capabilities = AMDGPU_PSP_RING_REQUIRED_CAPABILITIES;

	memory.capabilities &= ~AMDGPU_PSP_RING_DEVICE_SYNC_SUPPORTED;
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_NOT_SUPPORTED, "requires a device-visibility synchronization path");
	memory.capabilities = AMDGPU_PSP_RING_REQUIRED_CAPABILITIES;

	memory.cpu_address = NULL;
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_BAD_VALUE, "rejects a missing CPU mapping");
	memory.cpu_address = ring;

	memory.allocation_size--;
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_BAD_VALUE, "requires the upstream 4 KiB ring allocation size");
	memory.allocation_size = sizeof(ring);

	memory.gpu_address++;
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_BAD_VALUE, "rejects a misaligned GPU address");
	memory.gpu_address--;

	memory.gpu_address = UINT64_MAX & ~(uint64)(AMDGPU_PSP_VEGA10_RING_ALIGNMENT - 1);
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_BAD_VALUE, "rejects a GPU address range that wraps");
	memory.gpu_address = 0x100000;

	memory.capabilities |= 1u << 31;
	success &= Check(amdgpu_psp_validate_vega10_ring_memory(&memory)
		== B_OK, "ignores unrelated capability bits");

	if (!success)
		return 1;
	printf("AMDGPU PSP ring memory contract tests passed.\n");
	return 0;
}
