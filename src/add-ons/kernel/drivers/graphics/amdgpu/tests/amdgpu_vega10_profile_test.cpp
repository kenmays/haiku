/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_vega10_profile.h"

#include <stdio.h>
#include <string.h>

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
	amdgpu_pci_device device;
	memset(&device, 0, sizeof(device));
	device.pci.vendor_id = 0x1002;
	device.pci.device_id = 0x6860;
	device.asic_family = AMDGPU_ASIC_VEGA10;
	device.generation = AMDGPU_GENERATION_GCN;
	device.identification_flags = AMDGPU_PCI_ID_EXACT;

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

	amdgpu_vega10_profile profile;
	bool success = true;
	success &= Check(amdgpu_validate_vega10_profile(&device, &discovery,
		&profile) == B_OK && profile.asic_family == AMDGPU_ASIC_VEGA10
		&& profile.sdma_instances == 2 && profile.umc_instances == 4
		&& profile.gc_revision == 1,
		"Vega 10 exact required IP topology is accepted");

	blocks[0].revision = 2;
	success &= Check(amdgpu_validate_vega10_profile(&device, &discovery,
		&profile) == B_NOT_SUPPORTED,
		"unexpected GC IP revision is rejected");
	blocks[0].revision = 1;

	blocks[0].base_address_count = 0;
	success &= Check(amdgpu_validate_vega10_profile(&device, &discovery,
		&profile) == B_NOT_SUPPORTED,
		"required IP without register base metadata is rejected");
	blocks[0].base_address_count = 1;

	blocks[discovery.ip_count] = blocks[0];
	discovery.ip_count++;
	success &= Check(amdgpu_validate_vega10_profile(&device, &discovery,
		&profile) == B_BAD_DATA,
		"duplicate hardware ID and instance is rejected");
	discovery.ip_count--;

	device.identification_flags = AMDGPU_PCI_ID_IP_DISCOVERY;
	success &= Check(amdgpu_validate_vega10_profile(&device, &discovery,
		&profile) == B_NOT_SUPPORTED,
		"class-only PCI match cannot authorize a Vega 10 profile");

	if (!success)
		return 1;
	printf("AMDGPU Vega 10 profile tests passed.\n");
	return 0;
}
