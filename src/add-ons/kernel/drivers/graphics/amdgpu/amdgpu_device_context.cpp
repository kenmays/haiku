/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <string.h>

#include "amdgpu_device_context.h"


static bool
IsValidContext(const amdgpu_device_context* context)
{
	return context != NULL
		&& context->magic == AMDGPU_DEVICE_CONTEXT_MAGIC
		&& (context->state == AMDGPU_DEVICE_DISCOVERED
			|| context->state == AMDGPU_DEVICE_CLAIMED);
}


status_t
amdgpu_device_context_init(amdgpu_device_context* context,
	const amdgpu_pci_device* device)
{
	if (context == NULL || device == NULL)
		return B_BAD_VALUE;
	if (context->magic == AMDGPU_DEVICE_CONTEXT_MAGIC)
		return B_BUSY;

	// The caller owns this storage and must zero it before first use.
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
	context->state = AMDGPU_DEVICE_DISCOVERED;
	context->magic = AMDGPU_DEVICE_CONTEXT_MAGIC;
	return B_OK;
}


status_t
amdgpu_device_context_claim(pci_module_info* pci,
	amdgpu_device_context* context)
{
	if (!IsValidContext(context))
		return B_BAD_VALUE;
	if (context->state != AMDGPU_DEVICE_DISCOVERED)
		return B_BUSY;

	status_t status = amdgpu_claim_device(pci, &context->pci_device,
		&context->resources);
	if (status != B_OK)
		return status;

	context->state = AMDGPU_DEVICE_CLAIMED;
	return B_OK;
}


status_t
amdgpu_device_context_release(pci_module_info* pci,
	amdgpu_device_context* context)
{
	if (!IsValidContext(context) || pci == NULL)
		return B_BAD_VALUE;

	/*
	 * Resource release is deliberately first. If an area cannot be deleted
	 * or the PCI function cannot be unreserved, preserve the complete object
	 * so the caller can retry without losing ownership bookkeeping.
	 */
	status_t status = amdgpu_release_device(pci, &context->resources);
	if (status != B_OK)
		return status;

	status = amdgpu_firmware_context_reset(&context->firmware);
	if (status != B_OK)
		return status;

	memset(context, 0, sizeof(*context));
	return B_OK;
}
