/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../device_resources.h"

#include <stdio.h>
#include <string.h>

static area_id sMapResult = 7;
static status_t sUnmapResult = B_OK;
static uint32 sMapCalls = 0;
static uint32 sUnmapCalls = 0;
static bool sMapAddressNull = false;


static area_id
FakeMap(const char*, phys_addr_t, size_t, uint32, uint32,
	void** address)
{
	sMapCalls++;
	if (sMapResult >= 0 && !sMapAddressNull)
		*address = (void*)0x1000;
	else
		*address = NULL;
	return sMapResult;
}


static status_t
FakeUnmap(area_id)
{
	sUnmapCalls++;
	return sUnmapResult;
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
	const amdgpu_resource_mapping_ops ops = { FakeMap, FakeUnmap };
	bool success = true;
	amdgpu_pci_device device;
	memset(&device, 0, sizeof(device));
	device.pci.vendor_id = 0x1002;
	device.pci.device_id = 0x6860;
	device.pci.header_type = PCI_header_type_generic;
	device.pci.u.h0.base_registers_pci[5] = 0x80000000;
	device.pci.u.h0.base_register_flags[5] = PCI_address_type_32;
	device.mmio_bar_mask = 1 << 5;
	device.mmio_bar_size[5] = B_PAGE_SIZE;
	device.identification_flags = AMDGPU_PCI_ID_EXACT;
	device.asic_family = AMDGPU_ASIC_VEGA10;
	device.prefetchable_bar_mask = 1 << 5;

	amdgpu_device_resources resources;
	memset(&resources, 0, sizeof(resources));
	success &= Check(amdgpu_init_device_resources(&resources) == B_OK,
		"resource state initializes");
	resources.reserved = true;
	resources.pci = device.pci;

	sMapResult = B_NO_MEMORY;
	success &= Check(amdgpu_map_mmio_bar_with_ops(&device, &resources, 5,
		&ops) == B_NO_MEMORY && resources.mmio_area[5] < 0
		&& resources.mmio[5] == NULL,
		"map failure leaves no published mapping state");

	sMapResult = 7;
	success &= Check(amdgpu_map_mmio_bar_with_ops(&device, &resources, 5,
		&ops) == B_OK && resources.mmio_area[5] == 7
		&& resources.mmio[5] == (void*)0x1000,
		"successful mapping records area and address");

	sUnmapResult = B_ERROR;
	success &= Check(amdgpu_unmap_mmio_bar_with_ops(&resources, 5,
		&ops) == B_ERROR && resources.mmio_area[5] == 7
		&& resources.mmio[5] == (void*)0x1000,
		"failed unmap retains all state required for retry");

	sUnmapResult = B_OK;
	success &= Check(amdgpu_unmap_mmio_bar_with_ops(&resources, 5,
		&ops) == B_OK && resources.mmio_area[5] < 0
		&& resources.mmio[5] == NULL && resources.mmio_size[5] == 0,
		"successful retry clears mapping state");

	sMapResult = 8;
	sMapAddressNull = true;
	success &= Check(amdgpu_map_mmio_bar_with_ops(&device, &resources, 5,
		&ops) == B_NO_MEMORY && resources.mmio_area[5] == 8
		&& resources.mmio[5] == NULL,
		"area ID is retained when the OS returns no mapped address");
	success &= Check(amdgpu_unmap_mmio_bar_with_ops(&resources, 5,
		&ops) == B_OK && resources.mmio_area[5] < 0,
		"partially created area is still releasable");

	success &= Check(sMapCalls == 3 && sUnmapCalls == 3,
		"failure paths invoke exactly the expected OS operations");
	if (!success)
		return 1;
	printf("AMDGPU device resource mapping tests passed.\n");
	return 0;
}
