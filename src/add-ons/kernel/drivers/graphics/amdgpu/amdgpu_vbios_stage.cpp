/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <string.h>

#include "amdgpu_vbios_stage.h"

status_t
amdgpu_vbios_stage_init(amdgpu_vbios_stage* stage,
	amdgpu_firmware_context* context, void* buffer, size_t capacity)
{
	if (stage == NULL || context == NULL || buffer == NULL || capacity == 0
		|| context->magic != AMDGPU_FIRMWARE_CONTEXT_MAGIC
		|| context->state != AMDGPU_FIRMWARE_DEVICE_IDENTIFIED)
		return B_BAD_VALUE;

	memset(stage, 0, sizeof(*stage));
	stage->context = context;
	stage->buffer = buffer;
	stage->capacity = capacity;
	return B_OK;
}


status_t
amdgpu_vbios_stage_load_callback(void* cookie)
{
	amdgpu_vbios_stage* stage = (amdgpu_vbios_stage*)cookie;
	if (stage == NULL || stage->context == NULL || stage->buffer == NULL
		|| stage->capacity == 0)
		return B_BAD_VALUE;
	if (stage->loaded)
		return B_BUSY;

	/* A retry starts from a clean, fully bounded staging allocation. */
	memset(stage->buffer, 0, stage->capacity);
	stage->image_size = 0;
	status_t status = amdgpu_firmware_context_query(stage->context);
	if (status != B_OK)
		return status;
	if (stage->context->required_size > stage->capacity)
		return B_BUFFER_OVERFLOW;

	status = amdgpu_firmware_context_load(stage->context, stage->buffer,
		stage->capacity);
	if (status != B_OK) {
		/* Keep the complete staging allocation free of partial output. */
		memset(stage->buffer, 0, stage->capacity);
		stage->image_size = 0;
		return status;
	}

	stage->image_size = stage->context->required_size;
	stage->loaded = true;
	return B_OK;
}


status_t
amdgpu_vbios_stage_unload_callback(void* cookie)
{
	amdgpu_vbios_stage* stage = (amdgpu_vbios_stage*)cookie;
	if (stage == NULL || stage->context == NULL || stage->buffer == NULL
		|| stage->capacity == 0)
		return B_BAD_VALUE;
	if (!stage->loaded)
		return B_OK;

	/* Clear the complete bounded staging allocation, not only the ROM. */
	memset(stage->buffer, 0, stage->capacity);
	stage->image_size = 0;
	stage->loaded = false;

	amdgpu_pci_device device = stage->context->device;
	status_t status = amdgpu_firmware_context_reset(stage->context);
	if (status != B_OK)
		return status;
	return amdgpu_firmware_context_init(stage->context, &device);
}
