/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <string.h>

#include "amdgpu_vega10_profile.h"


struct required_ip {
	uint16 hardware_id;
	uint8 instance;
	uint8 major;
	uint8 minor;
	uint8 revision;
	bool requires_base_address;
	amdgpu_ip_block_role role;
};

/* Values cross-checked against Linux soc15_hw_ip.h and
 * amdgpu_discovery_set_ip_blocks() for CHIP_VEGA10. */
static const required_ip kVega10RequiredIPs[] = {
	{11, 0, 9, 0, 1, true, AMDGPU_IP_ROLE_GC},
	{108, 0, 6, 1, 0, true, AMDGPU_IP_ROLE_NBIO},
	{34, 0, 9, 0, 0, true, AMDGPU_IP_ROLE_MM_HUB},
	{35, 0, 9, 0, 0, true, AMDGPU_IP_ROLE_AT_HUB},
	{150, 0, 6, 0, 0, true, AMDGPU_IP_ROLE_UMC},
	{255, 0, 9, 0, 0, false, AMDGPU_IP_ROLE_MP0},
	{1, 0, 9, 0, 0, false, AMDGPU_IP_ROLE_MP1},
	{40, 0, 4, 0, 0, false, AMDGPU_IP_ROLE_OSSSYS},
	{41, 0, 4, 0, 0, false, AMDGPU_IP_ROLE_HDP},
	{42, 0, 4, 0, 0, true, AMDGPU_IP_ROLE_SDMA0},
	{43, 0, 4, 0, 0, true, AMDGPU_IP_ROLE_SDMA1},
	{46, 0, 2, 1, 0, false, AMDGPU_IP_ROLE_DF},
	{3, 0, 9, 0, 0, false, AMDGPU_IP_ROLE_THM},
	{4, 0, 9, 0, 0, false, AMDGPU_IP_ROLE_SMUIO},
	{12, 0, 7, 0, 0, false, AMDGPU_IP_ROLE_UVD},
	{32, 0, 4, 0, 0, false, AMDGPU_IP_ROLE_VCE},
	{15, 0, 12, 0, 0, false, AMDGPU_IP_ROLE_DCI}
};


static const amdgpu_ip_block*
FindIP(const amdgpu_ip_discovery_info* discovery,
	uint16 hardwareID, uint8 instance, uint32* matches)
{
	const amdgpu_ip_block* found = NULL;
	*matches = 0;
	for (uint32 i = 0; i < discovery->ip_count; i++) {
		const amdgpu_ip_block* block = &discovery->blocks[i];
		if (block->hardware_id == hardwareID && block->instance == instance) {
			found = block;
			(*matches)++;
		}
	}
	return found;
}


status_t
amdgpu_validate_vega10_profile(const amdgpu_pci_device* device,
	const amdgpu_ip_discovery_info* discovery,
	amdgpu_vega10_profile* profile)
{
	if (device == NULL || discovery == NULL || profile == NULL
		|| discovery->blocks == NULL || discovery->ip_count == 0
		|| discovery->ip_count > AMDGPU_IP_DISCOVERY_MAX_IPS
		|| discovery->ip_count > discovery->block_capacity
		|| discovery->version < 1 || discovery->version > 4
		|| discovery->die_count != 1) {
		return B_BAD_VALUE;
	}

	memset(profile, 0, sizeof(*profile));
	if (device->pci.vendor_id != 0x1002
		|| device->asic_family != AMDGPU_ASIC_VEGA10
		|| device->generation != AMDGPU_GENERATION_GCN
		|| (device->identification_flags & AMDGPU_PCI_ID_EXACT) == 0) {
		return B_NOT_SUPPORTED;
	}

	for (uint32 i = 0; i < sizeof(kVega10RequiredIPs)
		/ sizeof(kVega10RequiredIPs[0]); i++) {
		const required_ip& required = kVega10RequiredIPs[i];
		uint32 matches;
		const amdgpu_ip_block* block = FindIP(discovery,
			required.hardware_id, required.instance, &matches);
		if (matches != 1 || block == NULL)
			return B_BAD_DATA;
		if (block->major != required.major
			|| block->minor != required.minor
			|| block->revision != required.revision
			|| (required.requires_base_address
				&& block->base_address_count == 0)) {
			return B_NOT_SUPPORTED;
		}
	}

	/* Reject duplicate hardware-ID/instance pairs, including unknown IPs. */
	for (uint32 i = 0; i < discovery->ip_count; i++) {
		for (uint32 j = i + 1; j < discovery->ip_count; j++) {
			if (discovery->blocks[i].hardware_id
					== discovery->blocks[j].hardware_id
				&& discovery->blocks[i].instance
					== discovery->blocks[j].instance) {
				return B_BAD_DATA;
			}
		}
	}

	profile->asic_family = AMDGPU_ASIC_VEGA10;
	profile->ip_count = discovery->ip_count;
	profile->sdma_instances = 2;
	profile->umc_instances = 4;
	profile->gc_major = 9;
	profile->gc_minor = 0;
	profile->gc_revision = 1;
	return B_OK;
}
