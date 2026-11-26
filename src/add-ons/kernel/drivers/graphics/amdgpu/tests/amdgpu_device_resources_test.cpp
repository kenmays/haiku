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
static area_id sUnmapOrder[16];
static area_id sFailUnmapArea = -1;
static bool sMapAddressNull = false;
static status_t sReserveResult = B_OK;
static status_t sUnreserveResult = B_OK;
static uint32 sReserveCalls = 0;
static uint32 sUnreserveCalls = 0;


static status_t
FakeReserve(pci_module_info*, uint8, uint8, uint8, const char*, void*)
{
	sReserveCalls++;
	return sReserveResult;
}


static status_t
FakeUnreserve(pci_module_info*, uint8, uint8, uint8, const char*, void*)
{
	sUnreserveCalls++;
	return sUnreserveResult;
}


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
FakeUnmap(area_id area)
{
	if (sUnmapCalls < sizeof(sUnmapOrder) / sizeof(sUnmapOrder[0]))
		sUnmapOrder[sUnmapCalls] = area;
	sUnmapCalls++;
	if (area == sFailUnmapArea)
		return B_ERROR;
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
	const amdgpu_resource_mapping_ops ops = {
		FakeReserve, FakeUnreserve, FakeMap, FakeUnmap
	};
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
		"mapping failure paths invoke expected OS operations");

	pci_module_info pci;
	memset(&pci, 0, sizeof(pci));
	memset(&resources, 0, sizeof(resources));
	success &= Check(amdgpu_init_device_resources(&resources) == B_OK,
		"resource state reinitializes after complete cleanup");
	sReserveResult = B_BUSY;
	success &= Check(amdgpu_claim_device_with_ops(&pci, &device, &resources,
		&ops) == B_BUSY && !resources.reserved
		&& resources.magic == AMDGPU_DEVICE_RESOURCES_MAGIC,
		"failed PCI reservation leaves an initialized unclaimed object");

	sReserveResult = B_OK;
	success &= Check(amdgpu_claim_device_with_ops(&pci, &device, &resources,
		&ops) == B_OK && resources.reserved
		&& resources.pci.vendor_id == 0x1002,
		"successful PCI reservation records ownership");

	sUnreserveResult = B_ERROR;
	success &= Check(amdgpu_release_device_with_ops(&pci, &resources,
		&ops) == B_ERROR && resources.reserved,
		"failed PCI unreserve preserves ownership for retry");

	sUnreserveResult = B_OK;
	success &= Check(amdgpu_release_device_with_ops(&pci, &resources,
		&ops) == B_OK && !resources.reserved
		&& resources.magic == AMDGPU_DEVICE_RESOURCES_MAGIC,
		"successful PCI unreserve clears resource state");
	success &= Check(sReserveCalls == 2 && sUnreserveCalls == 2,
		"PCI failure and retry paths invoke expected callbacks");

	/*
	 * A failed high BAR teardown must stop release before lower BARs and
	 * before PCI unreservation. The retained resource state must permit a
	 * complete retry in descending BAR order.
	 */
	memset(&resources, 0, sizeof(resources));
	success &= Check(amdgpu_init_device_resources(&resources) == B_OK,
		"resource state initializes for multi-BAR release test");
	resources.reserved = true;
	resources.pci = device.pci;
	resources.mmio_area[1] = 11;
	resources.mmio[1] = (void*)0x1100;
	resources.mmio_physical[1] = 0x81000000;
	resources.mmio_size[1] = B_PAGE_SIZE;
	resources.mmio_area[3] = 33;
	resources.mmio[3] = (void*)0x3300;
	resources.mmio_physical[3] = 0x83000000;
	resources.mmio_size[3] = B_PAGE_SIZE;
	sUnmapCalls = 0;
	sUnreserveCalls = 0;
	sUnmapResult = B_OK;
	sFailUnmapArea = 33;
	success &= Check(amdgpu_release_device_with_ops(&pci, &resources,
		&ops) == B_ERROR && resources.reserved
		&& resources.mmio_area[3] == 33 && resources.mmio_area[1] == 11
		&& sUnmapCalls == 1 && sUnreserveCalls == 0
		&& sUnmapOrder[0] == 33,
		"failed high-BAR unmap retains lower BARs and skips PCI unreserve");

	sFailUnmapArea = -1;
	sUnmapCalls = 0;
	success &= Check(amdgpu_release_device_with_ops(&pci, &resources,
		&ops) == B_OK && !resources.reserved
		&& resources.mmio_area[1] == -1 && resources.mmio_area[3] == -1
		&& sUnmapCalls == 2 && sUnmapOrder[0] == 33
		&& sUnmapOrder[1] == 11 && sUnreserveCalls == 1,
		"release retry unmaps remaining BARs in reverse order before unreserve");
	if (!success)
		return 1;
	printf("AMDGPU device resource lifecycle tests passed.\n");
	return 0;
}
