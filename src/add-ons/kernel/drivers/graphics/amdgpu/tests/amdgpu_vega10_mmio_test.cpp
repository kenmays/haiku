/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <stdio.h>
#include <string.h>

#include "../amdgpu_vega10_mmio.h"


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
	static const uint16 kIDs[] = {
		11,108,34,35,150,255,1,40,41,42,43,46,3,4,12,32,15
	};
	static const uint8 kMajor[] = {
		9,6,9,9,6,9,9,4,4,4,4,2,9,9,7,4,12
	};
	static const uint8 kMinor[] = {
		0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0
	};
	static const uint8 kRevision[] = {
		1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	};

	bool success = true;
	amdgpu_pci_device device;
	memset(&device, 0, sizeof(device));
	device.pci.vendor_id = 0x1002;
	device.pci.device_id = 0x6860;
	device.pci.bus = 1;
	device.pci.device = 2;
	device.pci.function = 0;
	device.asic_family = AMDGPU_ASIC_VEGA10;
	device.generation = AMDGPU_GENERATION_GCN;
	device.identification_flags = AMDGPU_PCI_ID_EXACT;
	device.mmio_bar_mask = 1u << AMDGPU_VEGA10_REGISTER_BAR;
	device.mmio_bar_size[AMDGPU_VEGA10_REGISTER_BAR]
		= AMDGPU_VEGA10_MMIO_MIN_SIZE;

	amdgpu_ip_block blocks[32];
	memset(blocks, 0, sizeof(blocks));
	for (uint32 i = 0; i < sizeof(kIDs) / sizeof(kIDs[0]); i++) {
		blocks[i].hardware_id = kIDs[i];
		blocks[i].instance = 0;
		blocks[i].major = kMajor[i];
		blocks[i].minor = kMinor[i];
		blocks[i].revision = kRevision[i];
		blocks[i].base_address_count = 1;
		blocks[i].base_addresses[0] = 0x100 + i;
	}

	amdgpu_ip_discovery_info discovery = {};
	discovery.version = 4;
	discovery.die_count = 1;
	discovery.ip_count = sizeof(kIDs) / sizeof(kIDs[0]);
	discovery.blocks = blocks;
	discovery.block_capacity = 32;

	static uint8 mmio[AMDGPU_VEGA10_MMIO_MIN_SIZE];
	amdgpu_device_resources resources;
	memset(&resources, 0, sizeof(resources));
	resources.magic = AMDGPU_DEVICE_RESOURCES_MAGIC;
	resources.reserved = true;
	resources.pci = device.pci;
	resources.mmio_area[AMDGPU_VEGA10_REGISTER_BAR] = 1;
	resources.mmio[AMDGPU_VEGA10_REGISTER_BAR] = mmio;
	resources.mmio_physical[AMDGPU_VEGA10_REGISTER_BAR] = 0x100000;
	resources.mmio_size[AMDGPU_VEGA10_REGISTER_BAR] = sizeof(mmio);

	amdgpu_register_aperture aperture;
	memset(&aperture, 0, sizeof(aperture));
	success &= Check(amdgpu_vega10_resolve_register_aperture(&device,
		&resources, &discovery, &aperture) == B_OK,
		"validated Vega 10 resolves BAR 5 aperture");
	success &= Check(aperture.bar == 5 && aperture.offset == 0
		&& aperture.address == mmio && aperture.size == sizeof(mmio),
		"BAR 5 aperture identity and mapping are preserved");

	uint32 mappedOffset = 0;
	success &= Check(amdgpu_vega10_register_mapped_offset(&aperture,
		0x7effc, &mappedOffset) == B_OK && mappedOffset == 0x7effc,
		"last dword before register hole is accepted");
	success &= Check(amdgpu_vega10_register_mapped_offset(&aperture,
		0x7f000, &mappedOffset) == B_OK && mappedOffset == 0x7f000,
		"KFD remap page remains part of the linear kernel register aperture");
	success &= Check(amdgpu_vega10_register_mapped_offset(&aperture,
		0x7f100, &mappedOffset) == B_OK && mappedOffset == 0x7f100,
		"offsets after the KFD remap page remain linearly mapped");
	success &= Check(amdgpu_vega10_register_mapped_offset(&aperture,
		0x7effe, &mappedOffset) == B_BAD_VALUE,
		"unaligned dword access is rejected");

	resources.mmio_size[AMDGPU_VEGA10_REGISTER_BAR]
		= 0x9000;
	success &= Check(amdgpu_vega10_resolve_register_aperture(&device,
		&resources, &discovery, &aperture) == B_NOT_SUPPORTED,
		"BAR 5 mapping too small for the whitelisted probe register is rejected");
	resources.mmio_size[AMDGPU_VEGA10_REGISTER_BAR] = sizeof(mmio);

	device.identification_flags = AMDGPU_PCI_ID_IP_DISCOVERY;
	success &= Check(amdgpu_vega10_resolve_register_aperture(&device,
		&resources, &discovery, &aperture) == B_NOT_SUPPORTED,
		"class-only PCI candidate cannot resolve Vega 10 BAR 5");
	device.identification_flags = AMDGPU_PCI_ID_EXACT;

	resources.pci.function = 1;
	success &= Check(amdgpu_vega10_resolve_register_aperture(&device,
		&resources, &discovery, &aperture) == B_BAD_VALUE,
		"BAR 5 resources from another PCI function are rejected");
	resources.pci.function = 0;

	discovery.version = 5;
	success &= Check(amdgpu_vega10_resolve_register_aperture(&device,
		&resources, &discovery, &aperture) == B_BAD_VALUE,
		"invalid discovery topology is rejected before aperture resolution");

	if (!success)
		return 1;

	printf("AMDGPU Vega 10 MMIO aperture tests passed.\n");
	return 0;
}
