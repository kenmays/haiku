/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_INIT_SEQUENCE_H
#define AMDGPU_INIT_SEQUENCE_H

#include <SupportDefs.h>

/*
 * Ordered ASIC bring-up stages. Each successful stage owns resources until
 * its matching reverse-order callback succeeds.
 */
enum amdgpu_init_step {
	AMDGPU_INIT_STEP_NONE = 0,
	AMDGPU_INIT_STEP_DEVICE_CLAIMED = 1 << 0,
	AMDGPU_INIT_STEP_FIRMWARE_READY = 1 << 1,
	AMDGPU_INIT_STEP_REGISTERS_MAPPED = 1 << 2,
	AMDGPU_INIT_STEP_ASIC_INITIALIZED = 1 << 3
};

struct amdgpu_init_operations {
	status_t (*claim_device)(void* cookie);
	status_t (*load_firmware)(void* cookie);
	status_t (*map_registers)(void* cookie);
	status_t (*initialize_asic)(void* cookie);

	status_t (*shutdown_asic)(void* cookie);
	status_t (*unmap_registers)(void* cookie);
	status_t (*unload_firmware)(void* cookie);
	status_t (*release_device)(void* cookie);
};

struct amdgpu_init_sequence {
	uint32 completed_steps;
	status_t last_error;
	status_t rollback_error;
};

/* Initialize a fresh sequence before first use. */
void amdgpu_init_sequence_init(amdgpu_init_sequence* sequence);

/*
 * Run all four bring-up stages. On failure, unwind completed stages in
 * reverse order. The original bring-up error is returned; rollback_error
 * records a cleanup failure and completed_steps retains owned resources.
 */
status_t amdgpu_initialize_device(amdgpu_init_sequence* sequence,
	const amdgpu_init_operations* operations, void* cookie);

/*
 * Retry reverse-order cleanup. Stops at the first failed cleanup operation
 * because later resources may depend on the resource that failed to release.
 */
status_t amdgpu_unwind_device(amdgpu_init_sequence* sequence,
	const amdgpu_init_operations* operations, void* cookie);

#endif
