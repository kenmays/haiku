/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_device_context.h"

#include <stdio.h>
#include <string.h>

static status_t sClaimStatus = B_OK;
static status_t sReleaseStatus = B_OK;


status_t
amdgpu_init_device_resources(amdgpu_device_resources* resources)
{
	if (resources == NULL)
		return B_BAD_VALUE;
	memset(resources, 0, sizeof(*resources));
	resources->magic = AMDGPU_DEVICE_RESOURCES_MAGIC;
	for (uint32 i = 0; i < 6; i++)
		resources->mmio_area[i] = -1;
	return B_OK;
}


status_t
amdgpu_claim_device(pci_module_info*, const amdgpu_pci_device* device,
	amdgpu_device_resources* resources)
{
	if (device == NULL || resources == NULL)
		return B_BAD_VALUE;
	if (sClaimStatus != B_OK)
		return sClaimStatus;
	resources->reserved = true;
	resources->pci = device->pci;
	return B_OK;
}


status_t
amdgpu_release_device(pci_module_info*, amdgpu_device_resources* resources)
{
	if (resources == NULL)
		return B_BAD_VALUE;
	if (sReleaseStatus != B_OK)
		return sReleaseStatus;
	memset(resources, 0, sizeof(*resources));
	return B_OK;
}


status_t
amdgpu_firmware_context_init(amdgpu_firmware_context* context,
	const amdgpu_pci_device* device)
{
	if (context == NULL || device == NULL
		|| device->pci.vendor_id != 0x1002
		|| (device->identification_flags
			& (AMDGPU_PCI_ID_EXACT | AMDGPU_PCI_ID_IP_DISCOVERY)) == 0) {
		return B_BAD_VALUE;
	}
	memset(context, 0, sizeof(*context));
	context->magic = AMDGPU_FIRMWARE_CONTEXT_MAGIC;
	context->state = AMDGPU_FIRMWARE_DEVICE_IDENTIFIED;
	context->device = *device;
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


static bool
Check(bool condition, const char* name)
{
	if (!condition)
		fprintf(stderr, "FAIL: %s\n", name);
	return condition;
}


int
main()
{
	bool success = true;
	amdgpu_pci_device device;
	memset(&device, 0, sizeof(device));
	device.pci.vendor_id = 0x1002;
	device.pci.device_id = 0x73bf;
	device.pci.bus = 2;
	device.pci.device = 3;
	device.pci.function = 1;
	device.identification_flags = AMDGPU_PCI_ID_EXACT;

	amdgpu_device_context context;
	memset(&context, 0, sizeof(context));
	success &= Check(amdgpu_device_context_claim(NULL, &context)
		== B_BAD_VALUE, "uninitialized device context rejected");
	success &= Check(amdgpu_device_context_init(&context, &device) == B_OK
		&& context.state == AMDGPU_DEVICE_DISCOVERED
		&& !context.resources.reserved,
		"initialization binds metadata without claiming PCI");

	sClaimStatus = B_BUSY;
	success &= Check(amdgpu_device_context_claim(NULL, &context) == B_BUSY
		&& context.state == AMDGPU_DEVICE_DISCOVERED,
		"failed claim leaves device in discovered state");
	sClaimStatus = B_OK;
	success &= Check(amdgpu_device_context_claim(NULL, &context) == B_OK
		&& context.state == AMDGPU_DEVICE_CLAIMED
		&& context.resources.reserved,
		"explicit claim advances device state");

	sReleaseStatus = B_ERROR;
	success &= Check(amdgpu_device_context_release(
		(pci_module_info*)1, &context) == B_ERROR
		&& context.state == AMDGPU_DEVICE_CLAIMED
		&& context.resources.reserved,
		"failed release preserves ownership state for retry");
	sReleaseStatus = B_OK;
	success &= Check(amdgpu_device_context_release(
		(pci_module_info*)1, &context) == B_OK
		&& context.magic == 0
		&& context.state == AMDGPU_DEVICE_UNINITIALIZED,
		"successful release clears device and firmware context");

	device.pci.vendor_id = 0x1234;
	success &= Check(amdgpu_device_context_init(&context, &device)
		== B_BAD_VALUE, "non-AMD candidate rejected");

	if (!success)
		return 1;
	printf("AMDGPU device context tests passed.\n");
	return 0;
}
