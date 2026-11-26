/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_init_sequence.h"


void
amdgpu_init_sequence_init(amdgpu_init_sequence* sequence)
{
	if (sequence == NULL)
		return;
	sequence->completed_steps = AMDGPU_INIT_STEP_NONE;
	sequence->last_error = B_OK;
	sequence->rollback_error = B_OK;
}


static const uint32 kKnownInitSteps = AMDGPU_INIT_STEP_DEVICE_CLAIMED
	| AMDGPU_INIT_STEP_FIRMWARE_READY
	| AMDGPU_INIT_STEP_REGISTERS_MAPPED
	| AMDGPU_INIT_STEP_ASIC_INITIALIZED;


static bool
HasCompleteOperations(const amdgpu_init_operations* operations)
{
	return operations != NULL
		&& operations->claim_device != NULL
		&& operations->load_firmware != NULL
		&& operations->map_registers != NULL
		&& operations->initialize_asic != NULL
		&& operations->shutdown_asic != NULL
		&& operations->unmap_registers != NULL
		&& operations->unload_firmware != NULL
		&& operations->release_device != NULL;
}


status_t
amdgpu_unwind_device(amdgpu_init_sequence* sequence,
	const amdgpu_init_operations* operations, void* cookie)
{
	if (sequence == NULL || !HasCompleteOperations(operations)
		|| (sequence->completed_steps & ~kKnownInitSteps) != 0)
		return B_BAD_VALUE;

	sequence->rollback_error = B_OK;
	status_t status;
	if ((sequence->completed_steps & AMDGPU_INIT_STEP_ASIC_INITIALIZED) != 0) {
		status = operations->shutdown_asic(cookie);
		if (status != B_OK)
			goto failed;
		sequence->completed_steps &= ~AMDGPU_INIT_STEP_ASIC_INITIALIZED;
	}
	if ((sequence->completed_steps & AMDGPU_INIT_STEP_REGISTERS_MAPPED) != 0) {
		status = operations->unmap_registers(cookie);
		if (status != B_OK)
			goto failed;
		sequence->completed_steps &= ~AMDGPU_INIT_STEP_REGISTERS_MAPPED;
	}
	if ((sequence->completed_steps & AMDGPU_INIT_STEP_FIRMWARE_READY) != 0) {
		status = operations->unload_firmware(cookie);
		if (status != B_OK)
			goto failed;
		sequence->completed_steps &= ~AMDGPU_INIT_STEP_FIRMWARE_READY;
	}
	if ((sequence->completed_steps & AMDGPU_INIT_STEP_DEVICE_CLAIMED) != 0) {
		status = operations->release_device(cookie);
		if (status != B_OK)
			goto failed;
		sequence->completed_steps &= ~AMDGPU_INIT_STEP_DEVICE_CLAIMED;
	}
	return B_OK;

failed:
	sequence->rollback_error = status;
	return status;
}


status_t
amdgpu_initialize_device(amdgpu_init_sequence* sequence,
	const amdgpu_init_operations* operations, void* cookie)
{
	if (sequence == NULL || !HasCompleteOperations(operations)
		|| (sequence->completed_steps & ~kKnownInitSteps) != 0)
		return B_BAD_VALUE;
	if (sequence->completed_steps != AMDGPU_INIT_STEP_NONE)
		return B_BUSY;

	sequence->last_error = B_OK;
	sequence->rollback_error = B_OK;

	status_t status = operations->claim_device(cookie);
	if (status != B_OK)
		goto failed;
	sequence->completed_steps |= AMDGPU_INIT_STEP_DEVICE_CLAIMED;

	status = operations->load_firmware(cookie);
	if (status != B_OK)
		goto failed;
	sequence->completed_steps |= AMDGPU_INIT_STEP_FIRMWARE_READY;

	status = operations->map_registers(cookie);
	if (status != B_OK)
		goto failed;
	sequence->completed_steps |= AMDGPU_INIT_STEP_REGISTERS_MAPPED;

	status = operations->initialize_asic(cookie);
	if (status != B_OK)
		goto failed;
	sequence->completed_steps |= AMDGPU_INIT_STEP_ASIC_INITIALIZED;
	return B_OK;

failed:
	sequence->last_error = status;
	amdgpu_unwind_device(sequence, operations, cookie);
	return status;
}
