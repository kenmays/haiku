/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AMDGPU native Haiku driver foundation.
 *
 * This first stage deliberately performs PCI enumeration only. It does not
 * claim devices, map BARs, load firmware, or enable acceleration. Those
 * operations will be added behind validated, generation-specific stages.
 */

#include <Drivers.h>
#include <KernelExport.h>
#include <PCI.h>
#include <SupportDefs.h>

#include <stdio.h>


int32 api_version = B_CUR_DRIVER_API_VERSION;

static pci_module_info* sPCI = NULL;
static uint32 sAMDDisplayCount = 0;


static uint32
CountAMDDisplayDevices(void)
{
	uint32 count = 0;
	pci_info info;
	int32 index;

	for (index = 0; sPCI->get_nth_pci_info(index, &info) == B_OK; index++) {
		if (info.vendor_id != 0x1002)
			continue;

		if (info.class_base != PCI_display)
			continue;

		// Count display-class functions only. Do not assume that every AMD
		// display controller is a VGA-class function.
		count++;
	}

	return count;
}


extern "C" status_t
init_hardware(void)
{
	pci_module_info* pci = NULL;
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&pci);
	if (status != B_OK)
		return status;

	uint32 count = 0;
	pci_info info;
	int32 index;

	for (index = 0; pci->get_nth_pci_info(index, &info) == B_OK; index++) {
		if (info.vendor_id == 0x1002 && info.class_base == PCI_display)
			count++;
	}

	put_module(B_PCI_MODULE_NAME);
	return count > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


extern "C" status_t
init_driver(void)
{
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&sPCI);
	if (status != B_OK) {
		sPCI = NULL;
		return status;
	}

	sAMDDisplayCount = CountAMDDisplayDevices();
	if (sAMDDisplayCount == 0) {
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
		return B_ENTRY_NOT_FOUND;
	}

	dprintf("amdgpu: found %lu AMD display function(s); hardware bring-up "
		"is not enabled in this foundation build\\n", sAMDDisplayCount);

	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	if (sPCI != NULL) {
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
	}

	sAMDDisplayCount = 0;
}


// No device nodes are published until resource ownership, device lifetime,
// interrupt teardown, and the accelerant ABI are implemented.
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
