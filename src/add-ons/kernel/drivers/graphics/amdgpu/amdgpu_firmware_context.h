/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_FIRMWARE_CONTEXT_H
#define AMDGPU_FIRMWARE_CONTEXT_H

#include <SupportDefs.h>

#include "pci_device.h"
#include "amdgpu_vfct.h"

static const uint32 AMDGPU_FIRMWARE_CONTEXT_MAGIC = 0x414d4643;

enum amdgpu_firmware_state {
	AMDGPU_FIRMWARE_UNINITIALIZED = 0,
	AMDGPU_FIRMWARE_DEVICE_IDENTIFIED,
	AMDGPU_FIRMWARE_SIZE_KNOWN,
	AMDGPU_FIRMWARE_LOADED
};

/* Caller-owned per-function state; the BIOS buffer is never retained. */
struct amdgpu_firmware_context {
	uint32 magic;
	amdgpu_firmware_state state;
	amdgpu_pci_device device;
	amdgpu_vfct_info info;
	size_t required_size;
};

status_t amdgpu_firmware_context_init(amdgpu_firmware_context* context,
	const amdgpu_pci_device* device);
status_t amdgpu_firmware_context_query(amdgpu_firmware_context* context);
status_t amdgpu_firmware_context_load(amdgpu_firmware_context* context,
	void* biosBuffer, size_t biosCapacity);
status_t amdgpu_firmware_context_reset(amdgpu_firmware_context* context);

#endif
