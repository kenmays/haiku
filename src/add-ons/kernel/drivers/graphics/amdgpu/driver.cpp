/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AMDGPU native Haiku driver foundation.
 *
 * PCI enumeration and BAR metadata discovery only. No device is claimed,
 * no BAR is mapped, and no hardware register or PCI command bit is changed.
 */
#include <Drivers.h>
#include <KernelExport.h>
#include <PCI.h>
#include <SupportDefs.h>
#include <string.h>

#include "pci_device.h"

int32 api_version = B_CUR_DRIVER_API_VERSION;

static pci_module_info* sPCI = NULL;
static amdgpu_pci_device sDevices[AMDGPU_MAX_DEVICES];
static uint32 sDeviceCount = 0;

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

	sDeviceCount = 0;
	status = amdgpu_enumerate(sPCI, sDevices, AMDGPU_MAX_DEVICES,
		&sDeviceCount);
	if (status != B_OK) {
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
		return status;
	}

	uint32 exactIDs = 0;
	uint32 ipDiscovery = 0;
	for (uint32 i = 0; i < sDeviceCount; i++) {
		if ((sDevices[i].identification_flags & AMDGPU_PCI_ID_EXACT) != 0)
			exactIDs++;
		if ((sDevices[i].identification_flags
			& AMDGPU_PCI_ID_IP_DISCOVERY) != 0)
			ipDiscovery++;
	}
	dprintf("amdgpu: retained %" B_PRIu32 " AMD GPU function(s), "
		"%" B_PRIu32 " exact PCI IDs, %" B_PRIu32
		" IP-discovery class matches; no hardware initialized.\\n",
		sDeviceCount, exactIDs, ipDiscovery);
	return B_OK;
}

extern "C" void
uninit_driver(void)
{
	if (sPCI != NULL) {
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
	}
	sDeviceCount = 0;
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
