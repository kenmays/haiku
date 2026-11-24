/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <stdio.h>
#include <string.h>

#include "amdgpu_device_context.h"


static bool
IsValidContext(const amdgpu_device_context* context)
{
	return context != NULL
		&& context->magic == AMDGPU_DEVICE_CONTEXT_MAGIC
		&& context->lock_initialized
		&& context->state >= AMDGPU_DEVICE_DISCOVERED
		&& context->state <= AMDGPU_DEVICE_STOPPING;
}


status_t
amdgpu_device_context_init(amdgpu_device_context* context,
	const amdgpu_pci_device* device)
{
	if (context == NULL || device == NULL)
		return B_BAD_VALUE;
	if (context->magic == AMDGPU_DEVICE_CONTEXT_MAGIC)
		return B_BUSY;

	memset(context, 0, sizeof(*context));
	status_t status = amdgpu_init_device_resources(&context->resources);
	if (status != B_OK)
		return status;

	status = amdgpu_firmware_context_init(&context->firmware, device);
	if (status != B_OK) {
		memset(context, 0, sizeof(*context));
		return status;
	}

	context->pci_device = *device;
	sprintf(context->node_name, "graphics/amdgpu_%02x%02x%02x",
		device->pci.bus, device->pci.device, device->pci.function);
	mutex_init(&context->lock, "amdgpu device context");
	context->lock_initialized = true;
	context->state = AMDGPU_DEVICE_DISCOVERED;
	context->magic = AMDGPU_DEVICE_CONTEXT_MAGIC;
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

	// The reference is dropped by free(), after close-side work is complete.
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

	status = amdgpu_release_device(pci, &context->resources);
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

	context->magic = 0;
	context->state = AMDGPU_DEVICE_UNINITIALIZED;
	context->lock_initialized = false;
	mutex_unlock(&context->lock);
	mutex_destroy(&context->lock);
	memset(context, 0, sizeof(*context));
	return B_OK;
}
