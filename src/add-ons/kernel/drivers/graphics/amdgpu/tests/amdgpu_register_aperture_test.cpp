/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <stdio.h>
#include <string.h>

#include "../amdgpu_register_aperture.h"


static bool
Check(bool condition, const char* description)
{
	if (!condition)
		printf("FAIL: %s\n", description);
	return condition;
}


int
main()
{
	bool success = true;
	amdgpu_pci_device device;
	amdgpu_device_resources resources;
	uint8 mmio[4096];
	memset(&device, 0, sizeof(device));
	memset(&resources, 0, sizeof(resources));
	device.pci.vendor_id = 0x1002;
	device.pci.device_id = 0x6860;
	device.pci.bus = 1;
	device.pci.device = 2;
	device.pci.function = 0;
	device.mmio_bar_mask = 1u << 2;
	resources.magic = AMDGPU_DEVICE_RESOURCES_MAGIC;
	resources.reserved = true;
	resources.pci = device.pci;
	resources.mmio_area[2] = 1;
	resources.mmio[2] = mmio;
	resources.mmio_size[2] = sizeof(mmio);

	amdgpu_register_aperture aperture;
	memset(&aperture, 0, sizeof(aperture));
	success &= Check(amdgpu_resolve_register_aperture(&device, &resources,
		2, 0x100, 0x200, &aperture) == B_OK,
		"valid BAR-relative register aperture resolves");
	success &= Check(aperture.address == mmio + 0x100
		&& aperture.size == 0x200, "resolved window is BAR-relative");

	uint32 offset = 0;
	success &= Check(amdgpu_register_offset(&aperture, 0x1fc,
		&offset) == B_OK && offset == 0x1fc,
		"last in-range aligned dword is accepted");
	success &= Check(amdgpu_register_offset(&aperture, 0x1fe,
		&offset) == B_BAD_VALUE, "unaligned register offset is rejected");
	success &= Check(amdgpu_register_offset(&aperture, 0x200,
		&offset) == B_BAD_VALUE, "access crossing aperture end is rejected");
	success &= Check(amdgpu_register_offset(&aperture, ~(uint64)3,
		&offset) == B_BAD_VALUE, "overflow-sized register offset is rejected");

	success &= Check(amdgpu_resolve_register_aperture(&device, &resources,
		2, 0xff0, 0x20, &aperture) == B_BAD_VALUE,
		"BAR-relative window extending past mapping is rejected");
	success &= Check(amdgpu_resolve_register_aperture(&device, &resources,
		1, 0, 0x100, &aperture) == B_NOT_SUPPORTED,
		"unidentified BAR is rejected");

	device.prefetchable_bar_mask = 1u << 2;
	success &= Check(amdgpu_resolve_register_aperture(&device, &resources,
		2, 0, 0x100, &aperture) == B_NOT_SUPPORTED,
		"prefetchable BAR is rejected for register access");
	device.prefetchable_bar_mask = 0;

	resources.pci.function = 1;
	success &= Check(amdgpu_resolve_register_aperture(&device, &resources,
		2, 0, 0x100, &aperture) == B_BAD_VALUE,
		"resource belonging to another PCI function is rejected");
	resources.pci.function = 0;
	resources.mmio[2] = NULL;
	success &= Check(amdgpu_resolve_register_aperture(&device, &resources,
		2, 0, 0x100, &aperture) == B_NOT_ALLOWED,
		"unmapped BAR is rejected");

	printf("AMDGPU register aperture tests: %s\n",
		success ? "PASS" : "FAIL");
	return success ? 0 : 1;
}
