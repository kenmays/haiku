/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Per-function AMDGPU lifecycle coordinator. Discovery initializes this
 * object without claiming the PCI function or touching hardware.
 */
#ifndef AMDGPU_DEVICE_CONTEXT_H
#define AMDGPU_DEVICE_CONTEXT_H

#include <SupportDefs.h>
#include <PCI.h>

#include "pci_device.h"
#include "device_resources.h"
#include "amdgpu_firmware_context.h"

static const uint32 AMDGPU_DEVICE_CONTEXT_MAGIC = 0x414d4443;

enum amdgpu_device_state {
	AMDGPU_DEVICE_UNINITIALIZED = 0,
	AMDGPU_DEVICE_DISCOVERED,
	AMDGPU_DEVICE_CLAIMED
};

struct amdgpu_device_context {
	uint32 magic;
	amdgpu_device_state state;
	amdgpu_pci_device pci_device;
	amdgpu_device_resources resources;
	amdgpu_firmware_context firmware;
};

/*
 * Caller-owned storage must be zero-initialized before first initialization.
 * Initialization only binds validated metadata; it does not reserve PCI,
 * map BARs, enable bus mastering, or execute firmware.
 */
status_t amdgpu_device_context_init(amdgpu_device_context* context,
	const amdgpu_pci_device* device);

/* Explicit opt-in for a future fully initialized device-node path. */
status_t amdgpu_device_context_claim(pci_module_info* pci,
	amdgpu_device_context* context);

/* Releases mappings/reservation before clearing firmware and device state. */
status_t amdgpu_device_context_release(pci_module_info* pci,
	amdgpu_device_context* context);

#endif
