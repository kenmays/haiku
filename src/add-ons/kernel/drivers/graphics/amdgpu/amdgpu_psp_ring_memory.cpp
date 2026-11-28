/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_psp_ring_memory.h"


status_t
amdgpu_psp_validate_vega10_ring_memory(
	const amdgpu_psp_ring_memory* memory)
{
	if (memory == NULL)
		return B_BAD_VALUE;

	if (memory->memory_domain != AMDGPU_PSP_MEMORY_DOMAIN_VRAM)
		return B_NOT_SUPPORTED;

	if ((memory->capabilities & AMDGPU_PSP_RING_REQUIRED_CAPABILITIES)
		!= AMDGPU_PSP_RING_REQUIRED_CAPABILITIES)
		return B_NOT_SUPPORTED;

	if (memory->cpu_address == NULL
		|| memory->allocation_size != AMDGPU_PSP_VEGA10_RING_SIZE)
		return B_BAD_VALUE;

	if ((memory->gpu_address
			& (AMDGPU_PSP_VEGA10_RING_ALIGNMENT - 1)) != 0)
		return B_BAD_VALUE;

	if (memory->gpu_address > UINT64_MAX
		- (AMDGPU_PSP_VEGA10_RING_SIZE - 1))
		return B_BAD_VALUE;

	return B_OK;
}
