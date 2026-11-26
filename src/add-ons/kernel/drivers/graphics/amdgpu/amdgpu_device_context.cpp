/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "amdgpu_device_context.h"
#include "amdgpu_vega10_probe.h"


static bool
IsValidContext(const amdgpu_device_context* context)
{
	/* Mutable payload fields are checked only after acquiring context->lock. */
	return context != NULL && context->lock_initialized;
}


static void
ClearContextPayload(amdgpu_device_context* context)
{
	memset(&context->magic, 0,
		sizeof(*context) - offsetof(amdgpu_device_context, magic));
	context->lock_initialized = true;
}


status_t
amdgpu_device_context_init(amdgpu_device_context* context,
	const amdgpu_pci_device* device)
{
	if (context == NULL || device == NULL)
		return B_BAD_VALUE;

	if (context->lock_initialized) {
		status_t status = mutex_lock(&context->lock);
		if (status != B_OK)
			return status;
		if (context->magic == AMDGPU_DEVICE_CONTEXT_MAGIC) {
			mutex_unlock(&context->lock);
			return B_BUSY;
		}
		ClearContextPayload(context);
	} else {
		memset(context, 0, sizeof(*context));
		mutex_init(&context->lock, "amdgpu device context");
		context->lock_initialized = true;
		status_t lockStatus = mutex_lock(&context->lock);
		if (lockStatus != B_OK)
			return lockStatus;
	}

	status_t status = amdgpu_init_device_resources(&context->resources);
	if (status != B_OK) {
		ClearContextPayload(context);
		mutex_unlock(&context->lock);
		return status;
	}

	status = amdgpu_firmware_context_init(&context->firmware, device);
	if (status != B_OK) {
		ClearContextPayload(context);
		mutex_unlock(&context->lock);
		return status;
	}

	context->pci_device = *device;
	sprintf(context->node_name, "graphics/amdgpu_%02x%02x%02x",
		device->pci.bus, device->pci.device, device->pci.function);
	amdgpu_init_sequence_init(&context->init_sequence);
	context->init_sequence_active = false;
	context->init_cookie = NULL;
	memset(&context->init_operations, 0, sizeof(context->init_operations));
	context->state = AMDGPU_DEVICE_DISCOVERED;
	context->magic = AMDGPU_DEVICE_CONTEXT_MAGIC;
	mutex_unlock(&context->lock);
	return B_OK;
}

status_t
amdgpu_device_context_claim(pci_module_info* pci,
	amdgpu_device_context* context)
{
	if (!IsValidContext(context) || pci == NULL)
		return B_BAD_VALUE;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->state != AMDGPU_DEVICE_DISCOVERED) {
		mutex_unlock(&context->lock);
		return B_BUSY;
	}

	status = amdgpu_claim_device(pci, &context->pci_device,
		&context->resources);
	if (status == B_OK)
		context->state = AMDGPU_DEVICE_CLAIMED;
	mutex_unlock(&context->lock);
	return status;
}



status_t
amdgpu_device_context_initialize(amdgpu_device_context* context,
	const amdgpu_init_operations* operations, void* cookie)
{
	if (!IsValidContext(context) || operations == NULL)
		return B_BAD_VALUE;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->magic != AMDGPU_DEVICE_CONTEXT_MAGIC
		|| context->state != AMDGPU_DEVICE_DISCOVERED
		|| context->init_sequence_active) {
		mutex_unlock(&context->lock);
		return B_BUSY;
	}

	/*
	 * Callbacks operate on the supplied cookie and must use low-level
	 * resource helpers. The context lock serializes the complete sequence
	 * against opens, MMIO operations, and teardown.
	 */
	context->init_operations = *operations;
	context->init_cookie = cookie;
	amdgpu_init_sequence_init(&context->init_sequence);
	status = amdgpu_initialize_device(&context->init_sequence,
		&context->init_operations, context->init_cookie);
	if (context->init_sequence.completed_steps != AMDGPU_INIT_STEP_NONE)
		context->init_sequence_active = true;

	if (status == B_OK) {
		context->state = AMDGPU_DEVICE_CLAIMED;
	} else if (!context->init_sequence_active) {
		context->init_cookie = NULL;
		memset(&context->init_operations, 0,
			sizeof(context->init_operations));
	}

	mutex_unlock(&context->lock);
	return status;
}


status_t
amdgpu_device_context_map_mmio_bar(amdgpu_device_context* context,
	uint8 bar)
{
	if (!IsValidContext(context))
		return B_BAD_VALUE;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->state != AMDGPU_DEVICE_CLAIMED
		&& context->state != AMDGPU_DEVICE_NODE_READY) {
		mutex_unlock(&context->lock);
		return B_NOT_ALLOWED;
	}

	status = amdgpu_map_mmio_bar(&context->pci_device,
		&context->resources, bar);
	mutex_unlock(&context->lock);
	return status;
}


status_t
amdgpu_device_context_unmap_mmio_bar(amdgpu_device_context* context,
	uint8 bar)
{
	if (!IsValidContext(context))
		return B_BAD_VALUE;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->state != AMDGPU_DEVICE_CLAIMED
		&& context->state != AMDGPU_DEVICE_NODE_READY) {
		mutex_unlock(&context->lock);
		return B_NOT_ALLOWED;
	}

	status = amdgpu_unmap_mmio_bar(&context->resources, bar);
	mutex_unlock(&context->lock);
	return status;
}


status_t
amdgpu_device_context_read_vega10_gb_addr_config(
	amdgpu_device_context* context,
	const amdgpu_ip_discovery_info* discovery, uint32* value)
{
	if (!IsValidContext(context) || discovery == NULL || value == NULL)
		return B_BAD_VALUE;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->state != AMDGPU_DEVICE_CLAIMED
		&& context->state != AMDGPU_DEVICE_NODE_READY) {
		mutex_unlock(&context->lock);
		return B_NOT_ALLOWED;
	}

	/*
	 * Keep the context mutex held for the complete validation and MMIO load.
	 * Release/unmap paths take the same mutex, so the BAR area cannot be
	 * deleted while this read is in flight.
	 */
	status = amdgpu_vega10_read_gb_addr_config_unlocked(&context->pci_device,
		&context->resources, discovery, value);
	mutex_unlock(&context->lock);
	return status;
}


status_t
amdgpu_device_context_set_node_ready(amdgpu_device_context* context)
{
	if (!IsValidContext(context))
		return B_BAD_VALUE;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->state != AMDGPU_DEVICE_CLAIMED) {
		mutex_unlock(&context->lock);
		return B_NOT_ALLOWED;
	}
	if (!context->init_sequence_active
		|| (context->init_sequence.completed_steps
			& AMDGPU_INIT_STEP_ASIC_INITIALIZED) == 0) {
		mutex_unlock(&context->lock);
		return B_NOT_ALLOWED;
	}
	context->state = AMDGPU_DEVICE_NODE_READY;
	mutex_unlock(&context->lock);
	return B_OK;
}


bool
amdgpu_device_context_is_node_ready(amdgpu_device_context* context)
{
	if (!IsValidContext(context))
		return false;
	if (mutex_lock(&context->lock) != B_OK)
		return false;
	bool ready = context->state == AMDGPU_DEVICE_NODE_READY;
	mutex_unlock(&context->lock);
	return ready;
}


status_t
amdgpu_device_context_open(amdgpu_device_context* context, void** cookie)
{
	if (!IsValidContext(context) || cookie == NULL)
		return B_BAD_VALUE;
	*cookie = NULL;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->state != AMDGPU_DEVICE_NODE_READY) {
		mutex_unlock(&context->lock);
		return B_NOT_SUPPORTED;
	}
	if (context->open_count == 0xffffffffU) {
		mutex_unlock(&context->lock);
		return B_NO_MEMORY;
	}

	context->open_count++;
	*cookie = context;
	mutex_unlock(&context->lock);
	return B_OK;
}


status_t
amdgpu_device_context_close(void* cookie)
{
	amdgpu_device_context* context = (amdgpu_device_context*)cookie;
	if (!IsValidContext(context))
		return B_BAD_VALUE;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->magic != AMDGPU_DEVICE_CONTEXT_MAGIC
		|| context->open_count == 0) {
		mutex_unlock(&context->lock);
		return B_BAD_VALUE;
	}

	// The reference is dropped by free(), after close-side work is complete.
	mutex_unlock(&context->lock);
	return B_OK;
}


status_t
amdgpu_device_context_free(void* cookie)
{
	amdgpu_device_context* context = (amdgpu_device_context*)cookie;
	if (!IsValidContext(context))
		return B_BAD_VALUE;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->open_count == 0) {
		mutex_unlock(&context->lock);
		return B_BAD_VALUE;
	}
	context->open_count--;
	mutex_unlock(&context->lock);
	return B_OK;
}


status_t
amdgpu_device_context_release(pci_module_info* pci,
	amdgpu_device_context* context)
{
	if (!IsValidContext(context) || pci == NULL)
		return B_BAD_VALUE;

	status_t status = mutex_lock(&context->lock);
	if (status != B_OK)
		return status;
	if (context->open_count != 0) {
		mutex_unlock(&context->lock);
		return B_BUSY;
	}
	if (context->state == AMDGPU_DEVICE_STOPPING) {
		mutex_unlock(&context->lock);
		return B_BUSY;
	}

	const amdgpu_device_state previousState = context->state;
	context->state = AMDGPU_DEVICE_STOPPING;

	if (context->init_sequence_active) {
		status = amdgpu_unwind_device(&context->init_sequence,
			&context->init_operations, context->init_cookie);
		if (status == B_OK) {
			context->init_sequence_active = false;
			context->init_cookie = NULL;
			memset(&context->init_operations, 0,
				sizeof(context->init_operations));
		}
	} else {
		status = amdgpu_release_device(pci, &context->resources);
	}
	if (status != B_OK) {
		context->state = previousState;
		mutex_unlock(&context->lock);
		return status;
	}

	status = amdgpu_firmware_context_reset(&context->firmware);
	if (status != B_OK) {
		context->state = previousState;
		mutex_unlock(&context->lock);
		return status;
	}

	ClearContextPayload(context);
	mutex_unlock(&context->lock);
	return B_OK;
}
