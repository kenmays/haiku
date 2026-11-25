/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VEGA10_PROFILE_H
#define AMDGPU_VEGA10_PROFILE_H

#include <SupportDefs.h>

#include "amdgpu_discovery.h"
#include "pci_device.h"

enum amdgpu_ip_block_role {
	AMDGPU_IP_ROLE_GC = 0,
	AMDGPU_IP_ROLE_NBIO,
	AMDGPU_IP_ROLE_MM_HUB,
	AMDGPU_IP_ROLE_AT_HUB,
	AMDGPU_IP_ROLE_UMC,
	AMDGPU_IP_ROLE_MP0,
	AMDGPU_IP_ROLE_MP1,
	AMDGPU_IP_ROLE_OSSSYS,
	AMDGPU_IP_ROLE_HDP,
	AMDGPU_IP_ROLE_SDMA0,
	AMDGPU_IP_ROLE_SDMA1,
	AMDGPU_IP_ROLE_DF,
	AMDGPU_IP_ROLE_THM,
	AMDGPU_IP_ROLE_SMUIO,
	AMDGPU_IP_ROLE_UVD,
	AMDGPU_IP_ROLE_VCE,
	AMDGPU_IP_ROLE_DCI,
	AMDGPU_IP_ROLE_COUNT
};

struct amdgpu_vega10_profile {
	uint16 asic_family;
	uint16 ip_count;
	uint8 sdma_instances;
	uint8 umc_instances;
	uint8 gc_major;
	uint8 gc_minor;
	uint8 gc_revision;
};

/*
 * Validate a Vega 10 candidate against the expected IP discovery topology
 * and exact IP revisions. This is metadata validation only: it does not
 * authorize MMIO, firmware execution, PCI ownership, or node publication.
 */
status_t amdgpu_validate_vega10_profile(const amdgpu_pci_device* device,
	const amdgpu_ip_discovery_info* discovery,
	amdgpu_vega10_profile* profile);

#endif
