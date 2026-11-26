/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_device_context.h"

#include <stdio.h>
#include <string.h>
#include <lock.h>


void
mutex_init(mutex* lock, const char* name)
{
	memset(lock, 0, sizeof(*lock));
	lock->name = name;
}


void
mutex_destroy(mutex*)
{
}


status_t
mutex_lock(mutex*)
{
	sContextMutexHeld = true;
	return B_OK;
}


void
mutex_unlock(mutex*)
{
	sContextMutexHeld = false;
}

static status_t sClaimStatus = B_OK;
static status_t sReleaseStatus = B_OK;
static uint32 sMapCalls = 0;
static uint32 sUnmapCalls = 0;
static uint32 sProbeCalls = 0;
static bool sContextMutexHeld = false;
static bool sProbeObservedMutex = false;


status_t
amdgpu_map_mmio_bar(const amdgpu_pci_device*, amdgpu_device_resources*,
	uint8)
{
	sMapCalls++;
	return B_OK;
}


status_t
amdgpu_unmap_mmio_bar(amdgpu_device_resources*, uint8)
{
	sUnmapCalls++;
	return B_OK;
}


status_t
amdgpu_vega10_read_gb_addr_config(const amdgpu_pci_device*,
	const amdgpu_device_resources*,
	const amdgpu_ip_discovery_info*, uint32* value)
{
	sProbeCalls++;
	sProbeObservedMutex = sContextMutexHeld;
	*value = 0x12345678;
	return B_OK;
}


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
	pci_module_info* pci = (pci_module_info*)1;
	success &= Check(amdgpu_device_context_claim(NULL, &context)
		== B_BAD_VALUE, "uninitialized device context rejected");
	success &= Check(amdgpu_device_context_init(&context, &device) == B_OK
		&& context.state == AMDGPU_DEVICE_DISCOVERED
		&& !context.resources.reserved
		&& strcmp(context.node_name, "graphics/amdgpu_020301") == 0,
		"initialization binds metadata and stable BDF node name without claim");
	void* cookie = NULL;
	amdgpu_ip_discovery_info discovery;
	memset(&discovery, 0, sizeof(discovery));
	uint32 registerValue = 0;
	success &= Check(amdgpu_device_context_map_mmio_bar(&context, 5)
		== B_NOT_ALLOWED
		&& amdgpu_device_context_unmap_mmio_bar(&context, 5)
		== B_NOT_ALLOWED
		&& amdgpu_device_context_read_vega10_gb_addr_config(&context,
			&discovery, &registerValue) == B_NOT_ALLOWED
		&& sMapCalls == 0 && sUnmapCalls == 0 && sProbeCalls == 0,
		"MMIO operations are blocked before PCI claim");
	success &= Check(!amdgpu_device_context_is_node_ready(&context)
		&& amdgpu_device_context_set_node_ready(&context) == B_NOT_ALLOWED,
		"node cannot become ready before PCI ownership is established");
	success &= Check(amdgpu_device_context_open(&context, &cookie)
		== B_NOT_SUPPORTED && cookie == NULL,
		"discovered but uninitialized node cannot be opened");

	sClaimStatus = B_BUSY;
	success &= Check(amdgpu_device_context_claim(pci, &context) == B_BUSY
		&& context.state == AMDGPU_DEVICE_DISCOVERED,
		"failed claim leaves device in discovered state");
	sClaimStatus = B_OK;
	success &= Check(amdgpu_device_context_claim(pci, &context) == B_OK
		&& context.state == AMDGPU_DEVICE_CLAIMED
		&& context.resources.reserved,
		"explicit claim advances device state");
	success &= Check(amdgpu_device_context_map_mmio_bar(&context, 5)
		== B_OK && sMapCalls == 1,
		"BAR mapping is routed through the claimed context");
	success &= Check(amdgpu_device_context_read_vega10_gb_addr_config(
		&context, &discovery, &registerValue) == B_OK
		&& sProbeCalls == 1 && sProbeObservedMutex
		&& registerValue == 0x12345678,
		"read-only probe is executed through the claimed context");
	success &= Check(amdgpu_device_context_unmap_mmio_bar(&context, 5)
		== B_OK && sUnmapCalls == 1,
		"BAR unmapping is routed through the claimed context");
	success &= Check(amdgpu_device_context_set_node_ready(&context) == B_OK
		&& context.state == AMDGPU_DEVICE_NODE_READY
		&& amdgpu_device_context_is_node_ready(&context),
		"node becomes ready only after claim and initialization");
	success &= Check(amdgpu_device_context_open(&context, &cookie) == B_OK
		&& cookie == &context && context.open_count == 1,
		"open acquires a per-device lifetime reference");
	success &= Check(amdgpu_device_context_release(
		(pci_module_info*)1, &context) == B_BUSY,
		"teardown refuses to release a live open cookie");
	success &= Check(amdgpu_device_context_close(cookie) == B_OK
		&& amdgpu_device_context_free(cookie) == B_OK
		&& context.open_count == 0,
		"close and free release the node lifetime reference");

	sReleaseStatus = B_ERROR;
	success &= Check(amdgpu_device_context_release(
		(pci_module_info*)1, &context) == B_ERROR
		&& context.state == AMDGPU_DEVICE_NODE_READY
		&& context.resources.reserved,
		"failed release preserves ownership state for retry");
	sReleaseStatus = B_OK;
	success &= Check(amdgpu_device_context_release(
		(pci_module_info*)1, &context) == B_OK
		&& context.magic == 0
		&& context.state == AMDGPU_DEVICE_UNINITIALIZED
		&& context.lock_initialized
		&& !amdgpu_device_context_is_node_ready(&context),
		"successful release clears payload but preserves the reusable lock");

	device.pci.vendor_id = 0x1234;
	success &= Check(amdgpu_device_context_init(&context, &device)
		== B_BAD_VALUE, "non-AMD candidate rejected");

	if (!success)
		return 1;
	printf("AMDGPU device context tests passed.\n");
	return 0;
}
