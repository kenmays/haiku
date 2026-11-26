/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <ByteOrder.h>
#include <stdio.h>
#include <string.h>

#include "../amdgpu_vega10_probe.h"


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

	static uint32 mmio[AMDGPU_VEGA10_MMIO_MIN_SIZE / sizeof(uint32)];
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

	amdgpu_device_resources resources;
	memset(&resources, 0, sizeof(resources));
	resources.magic = AMDGPU_DEVICE_RESOURCES_MAGIC;
	resources.reserved = true;
	resources.pci = device.pci;
	resources.mmio_area[AMDGPU_VEGA10_REGISTER_BAR] = 1;
	resources.mmio[AMDGPU_VEGA10_REGISTER_BAR] = mmio;
	resources.mmio_physical[AMDGPU_VEGA10_REGISTER_BAR] = 0x100000;
	resources.mmio_size[AMDGPU_VEGA10_REGISTER_BAR] = sizeof(mmio);

	/* GC base 0x2000 + mmGB_ADDR_CONFIG 0x063e, in dword units. */
	static const uint32 kGBAddrConfigByteOffset = 0x98f8;
	mmio[kGBAddrConfigByteOffset / sizeof(uint32)]
		= B_HOST_TO_LENDIAN_INT32(0x12345678);

	uint32 value = 0;
	status_t status = amdgpu_vega10_read_gb_addr_config(&device,
		&resources, &discovery, &value);
	if (status != B_OK || value != 0x12345678) {
		fprintf(stderr, "FAIL: read-only GB_ADDR_CONFIG probe (%ld, 0x%08lx)\n",
			(long)status, (unsigned long)value);
		return 1;
	}

	if (amdgpu_vega10_read_gb_addr_config(&device, &resources,
		&discovery, NULL) != B_BAD_VALUE) {
		fprintf(stderr, "FAIL: null output rejected\n");
		return 1;
	}

	device.identification_flags = AMDGPU_PCI_ID_IP_DISCOVERY;
	if (amdgpu_vega10_read_gb_addr_config(&device, &resources,
		&discovery, &value) != B_NOT_SUPPORTED) {
		fprintf(stderr, "FAIL: class-only PCI match rejected\n");
		return 1;
	}

	device.identification_flags = AMDGPU_PCI_ID_EXACT;
	discovery.version = 5;
	if (amdgpu_vega10_read_gb_addr_config(&device, &resources,
		&discovery, &value) != B_BAD_VALUE) {
		fprintf(stderr, "FAIL: malformed discovery table rejected\n");
		return 1;
	}

	printf("AMDGPU Vega 10 read-only probe tests passed.\n");
	return 0;
}
