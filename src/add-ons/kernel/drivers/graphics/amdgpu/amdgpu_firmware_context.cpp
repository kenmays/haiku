/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Caller-owned per-PCI-function VBIOS state machine. No allocations, PCI
 * reservation, BAR mapping, or firmware execution are performed here.
 */
#include <string.h>

#include "amdgpu_firmware_context.h"
#include "amdgpu_vfct_provider.h"


static bool
IsValidContext(const amdgpu_firmware_context* context)
{
	return context != NULL
		&& context->magic == AMDGPU_FIRMWARE_CONTEXT_MAGIC
		&& context->state >= AMDGPU_FIRMWARE_DEVICE_IDENTIFIED
		&& context->state <= AMDGPU_FIRMWARE_LOADED;
}


static bool
MatchesDevice(const amdgpu_firmware_context* context,
	const amdgpu_vfct_info& info)
{
	return info.vendor_id == context->device.pci.vendor_id
		&& info.device_id == context->device.pci.device_id
		&& info.vbios.vendor_id == context->device.pci.vendor_id
		&& info.vbios.device_id == context->device.pci.device_id;
}

static bool
MatchesQuery(const amdgpu_firmware_context* context,
	const amdgpu_vfct_info& info)
{
	const amdgpu_vfct_info& queried = context->info;
	return info.image_offset == queried.image_offset
		&& info.image_size == queried.image_size
		&& info.entry_offset == queried.entry_offset
		&& info.bus == queried.bus
		&& info.device == queried.device
		&& info.function == queried.function
		&& info.vendor_id == queried.vendor_id
		&& info.device_id == queried.device_id
		&& info.subsystem_vendor_id == queried.subsystem_vendor_id
		&& info.subsystem_device_id == queried.subsystem_device_id
		&& info.revision == queried.revision
		&& info.bus_matches == queried.bus_matches;
}


status_t
amdgpu_firmware_context_init(amdgpu_firmware_context* context,
	const amdgpu_pci_device* device)
{
	if (context == NULL || device == NULL)
		return B_BAD_VALUE;

	amdgpu_vfct_device identity;
	status_t status = amdgpu_vfct_device_from_pci_device(device, &identity);
	if (status != B_OK)
		return status;

	memset(context, 0, sizeof(*context));
	context->device = *device;
	context->state = AMDGPU_FIRMWARE_DEVICE_IDENTIFIED;
	context->magic = AMDGPU_FIRMWARE_CONTEXT_MAGIC;
	return B_OK;
}


status_t
amdgpu_firmware_context_query(amdgpu_firmware_context* context)
{
	if (!IsValidContext(context))
		return B_BAD_VALUE;
	if (context->state == AMDGPU_FIRMWARE_LOADED)
		return B_BUSY;

	// A failed refresh must not leave an older image size usable for loading.
	memset(&context->info, 0, sizeof(context->info));
	context->required_size = 0;
	context->state = AMDGPU_FIRMWARE_DEVICE_IDENTIFIED;

	size_t requiredSize = 0;
	amdgpu_vfct_info info;
	status_t status = amdgpu_query_vbios_size_for_pci_device(
		&context->device, &requiredSize, &info);
	if (status != B_BUFFER_OVERFLOW)
		return status == B_OK ? B_BAD_DATA : status;
	if (requiredSize == 0 || info.image_size != requiredSize
		|| !MatchesDevice(context, info)) {
		return B_BAD_DATA;
	}

	context->info = info;
	context->required_size = requiredSize;
	context->state = AMDGPU_FIRMWARE_SIZE_KNOWN;
	return B_OK;
}


status_t
amdgpu_firmware_context_load(amdgpu_firmware_context* context,
	void* biosBuffer, size_t biosCapacity)
{
	if (!IsValidContext(context) || biosBuffer == NULL)
		return B_BAD_VALUE;
	if (context->state != AMDGPU_FIRMWARE_SIZE_KNOWN)
		return B_NOT_ALLOWED;
	if (biosCapacity < context->required_size)
		return B_BUFFER_OVERFLOW;

	size_t imageSize = 0;
	amdgpu_vfct_info info;
	status_t status = amdgpu_load_vbios_for_pci_device(
		&context->device, biosBuffer, biosCapacity, &imageSize, &info);
	if (status != B_OK) {
		/* A provider may have copied a prefix before reporting failure. */
		memset(biosBuffer, 0, context->required_size);
		return status;
	}
	if (imageSize != context->required_size
		|| info.image_size != context->required_size
		|| !MatchesDevice(context, info)
		|| !MatchesQuery(context, info)) {
		size_t clearSize = imageSize < biosCapacity ? imageSize : biosCapacity;
		memset(biosBuffer, 0, clearSize);
		return B_BAD_DATA;
	}

	/*
	 * The VFCT provider has already bounded the image extent, but validate
	 * the copied bytes again at the ownership boundary. A matching VFCT
	 * record alone is not enough: require a complete, checksummed PCI ROM
	 * chain containing an AMD ATOMBIOS image for this exact device.
	 */
	amdgpu_vbios_info parsedVbios;
	status = amdgpu_parse_vbios_image(biosBuffer, imageSize, &parsedVbios);
	if (status != B_OK || parsedVbios.vendor_id != context->device.pci.vendor_id
		|| parsedVbios.device_id != context->device.pci.device_id) {
		memset(biosBuffer, 0, imageSize);
		return B_BAD_DATA;
	}

	context->info = info;
	context->info.vbios = parsedVbios;
	context->state = AMDGPU_FIRMWARE_LOADED;
	return B_OK;
}


status_t
amdgpu_firmware_context_reset(amdgpu_firmware_context* context)
{
	if (context == NULL)
		return B_BAD_VALUE;
	memset(context, 0, sizeof(*context));
	return B_OK;
}
