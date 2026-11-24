/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AMDGPU native Haiku driver foundation.
 *
 * PCI enumeration and per-function context initialization only. No device
 * is claimed, no BAR is mapped, and no hardware register or PCI command bit
 * is changed. Device publication remains disabled until a real node and
 * accelerant initialization/teardown path exist.
 */
#include <Drivers.h>
#include <KernelExport.h>
#include <PCI.h>
#include <SupportDefs.h>
#include <string.h>

#include "amdgpu_device_context.h"

int32 api_version = B_CUR_DRIVER_API_VERSION;

static pci_module_info* sPCI = NULL;
static amdgpu_device_context sDevices[AMDGPU_MAX_DEVICES];
static uint32 sDeviceCount = 0;


static void
ReleaseInitializedDevices()
{
	while (sDeviceCount > 0) {
		uint32 index = sDeviceCount - 1;
		status_t status = amdgpu_device_context_release(sPCI,
			&sDevices[index]);
		if (status != B_OK) {
			dprintf("amdgpu: failed to unwind device context %" B_PRIu32
				": %" B_PRId32 "\n", index, status);
			return;
		}
		sDeviceCount--;
	}
}


extern "C" status_t
init_hardware(void)
{
	pci_module_info* pci = NULL;
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&pci);
	if (status != B_OK)
		return status;

	uint32 count = 0;
	status = amdgpu_enumerate(pci, NULL, 0, &count);
	put_module(B_PCI_MODULE_NAME);
	return status;
}


extern "C" status_t
init_driver(void)
{
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&sPCI);
	if (status != B_OK) {
		sPCI = NULL;
		return status;
	}

	amdgpu_pci_device candidates[AMDGPU_MAX_DEVICES];
	uint32 candidateCount = 0;
	status = amdgpu_enumerate(sPCI, candidates, AMDGPU_MAX_DEVICES,
		&candidateCount);
	if (status != B_OK) {
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
		return status;
	}

	memset(sDevices, 0, sizeof(sDevices));
	sDeviceCount = 0;
	uint32 exactIDs = 0;
	uint32 ipDiscovery = 0;
	for (uint32 i = 0; i < candidateCount; i++) {
		status = amdgpu_device_context_init(&sDevices[sDeviceCount],
			&candidates[i]);
		if (status != B_OK) {
			dprintf("amdgpu: context initialization failed for candidate "
				"%" B_PRIu32 ": %" B_PRId32 "\n", i, status);
			ReleaseInitializedDevices();
			if (sDeviceCount == 0) {
				put_module(B_PCI_MODULE_NAME);
				sPCI = NULL;
			}
			return status;
		}

		sDeviceCount++;
		if ((candidates[i].identification_flags & AMDGPU_PCI_ID_EXACT) != 0)
			exactIDs++;
		if ((candidates[i].identification_flags
			& AMDGPU_PCI_ID_IP_DISCOVERY) != 0)
			ipDiscovery++;
	}

	dprintf("amdgpu: retained %" B_PRIu32 " AMD GPU function(s), "
		"%" B_PRIu32 " exact PCI IDs, %" B_PRIu32
		" IP-discovery class matches; contexts initialized, "
		"no hardware claimed.\n",
		sDeviceCount, exactIDs, ipDiscovery);
	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	if (sPCI != NULL) {
		ReleaseInitializedDevices();
		if (sDeviceCount != 0)
			return;
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
	}
	memset(sDevices, 0, sizeof(sDevices));
}


extern "C" const char**
publish_devices(void)
{
	return NULL;
}


extern "C" device_hooks*
find_device(const char* /*name*/)
{
	return NULL;
}
