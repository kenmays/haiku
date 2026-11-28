/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_PSP_RING_MEMORY_H
#define AMDGPU_PSP_RING_MEMORY_H

#include <SupportDefs.h>

/*
 * Vega 10 PSP 3.1 uses a 4 KiB kernel-mode PSP ring allocated from VRAM.
 * This descriptor is a validation contract only; it does not allocate,
 * map, synchronize, or make the ring visible to the PSP.
 */
static const uint32 AMDGPU_PSP_VEGA10_RING_SIZE = 0x1000;
static const uint32 AMDGPU_PSP_VEGA10_RING_ALIGNMENT = 0x1000;

enum amdgpu_psp_memory_domain {
	AMDGPU_PSP_MEMORY_DOMAIN_UNKNOWN = 0,
	AMDGPU_PSP_MEMORY_DOMAIN_SYSTEM = 1,
	AMDGPU_PSP_MEMORY_DOMAIN_VRAM = 2
};

enum {
	AMDGPU_PSP_RING_CPU_MAPPED = 1 << 0,
	AMDGPU_PSP_RING_GPU_ADDRESS_VALID = 1 << 1,
	AMDGPU_PSP_RING_CONTIGUOUS = 1 << 2,
	AMDGPU_PSP_RING_DEVICE_SYNC_SUPPORTED = 1 << 3,
	AMDGPU_PSP_RING_REQUIRED_CAPABILITIES = AMDGPU_PSP_RING_CPU_MAPPED
		| AMDGPU_PSP_RING_GPU_ADDRESS_VALID | AMDGPU_PSP_RING_CONTIGUOUS
		| AMDGPU_PSP_RING_DEVICE_SYNC_SUPPORTED
};

struct amdgpu_psp_ring_memory {
	uint64 gpu_address;
	void* cpu_address;
	size_t allocation_size;
	uint32 memory_domain;
	uint32 capabilities;
};

/*
 * Validate a backend-supplied Vega 10 PSP 3.1 ring allocation.
 *
 * The backend must guarantee a CPU mapping, a valid device-visible address,
 * contiguous device-visible VRAM backing, and a mechanism to make CPU writes visible to the
 * PSP before use. Haiku's generic system-memory physical map is not a
 * substitute for a VRAM allocation or a GPU bus address.
 */
status_t amdgpu_psp_validate_vega10_ring_memory(
	const amdgpu_psp_ring_memory* memory);

#endif
