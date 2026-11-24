/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_DEVICE_CONTEXT_H
#define AMDGPU_DEVICE_CONTEXT_H

#include <Drivers.h>
#include <SupportDefs.h>
#include <PCI.h>
#include <lock.h>

#include "pci_device.h"
#include "device_resources.h"
#include "amdgpu_firmware_context.h"

static const uint32 AMDGPU_DEVICE_CONTEXT_MAGIC = 0x414d4443;

enum amdgpu_device_state {
	AMDGPU_DEVICE_UNINITIALIZED = 0,
	AMDGPU_DEVICE_DISCOVERED,
	AMDGPU_DEVICE_CLAIMED,
	AMDGPU_DEVICE_NODE_READY,
	AMDGPU_DEVICE_STOPPING
};

struct amdgpu_device_context {
	/* First member so the payload can be reset without clobbering the lock. */
	mutex lock;
	uint32 magic;
	amdgpu_device_state state;
	amdgpu_pci_device pci_device;
	amdgpu_device_resources resources;
	amdgpu_firmware_context firmware;
	bool lock_initialized;
	uint32 open_count;
	char node_name[64];
};

status_t amdgpu_device_context_init(amdgpu_device_context* context,
	const amdgpu_pci_device* device);
status_t amdgpu_device_context_claim(pci_module_info* pci,
	amdgpu_device_context* context);

/* Called only after ASIC initialization and node backing are complete. */
status_t amdgpu_device_context_set_node_ready(
	amdgpu_device_context* context);
bool amdgpu_device_context_is_node_ready(amdgpu_device_context* context);

/* Open/free hooks hold a reference so teardown cannot free a live context. */
status_t amdgpu_device_context_open(amdgpu_device_context* context,
	void** cookie);
status_t amdgpu_device_context_close(void* cookie);
status_t amdgpu_device_context_free(void* cookie);

/* Refuses teardown while open references exist; preserves state on failure. */
status_t amdgpu_device_context_release(pci_module_info* pci,
	amdgpu_device_context* context);

#endif
