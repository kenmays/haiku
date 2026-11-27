/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VBIOS_STAGE_H
#define AMDGPU_VBIOS_STAGE_H

#include <SupportDefs.h>

#include "amdgpu_firmware_context.h"

/*
 * Caller-owned, bounded staging for a validated PCI option ROM. This is not
 * a PSP firmware loader and does not submit or execute firmware on the GPU.
 * The caller owns the storage and must keep it alive through coordinator
 * teardown.
 */
struct amdgpu_vbios_stage {
	amdgpu_firmware_context* context;
	void* buffer;
	size_t capacity;
	size_t image_size;
	bool loaded;
};

status_t amdgpu_vbios_stage_init(amdgpu_vbios_stage* stage,
	amdgpu_firmware_context* context, void* buffer, size_t capacity);
status_t amdgpu_vbios_stage_load_callback(void* cookie);
status_t amdgpu_vbios_stage_unload_callback(void* cookie);

#endif
