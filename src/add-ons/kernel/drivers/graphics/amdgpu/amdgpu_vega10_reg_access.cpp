/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <SupportDefs.h>

#include "amdgpu_vega10_reg_access.h"
#include "amdgpu_vega10_mmio.h"

/*
 * The initial table is intentionally tiny. GB_ADDR_CONFIG is a stable GC
 * configuration register used by the read-only bring-up probe. Do not add
 * status, interrupt, FIFO, doorbell, reset, index/data, or write-one-to-clear
 * registers without reviewing their read and write side effects.
 */
struct vega10_register_policy {
	uint16 hardware_id;
	uint8 instance;
	uint8 segment;
	uint32 register_index;
	uint8 access_mask;
};

enum {
	AMDGPU_REGISTER_POLICY_READ = 1 << 0,
	AMDGPU_REGISTER_POLICY_WRITE = 1 << 1
};

static const vega10_register_policy kVega10RegisterPolicy[] = {
	{11, 0, 0, AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX,
		AMDGPU_REGISTER_POLICY_READ}
};


status_t
amdgpu_vega10_check_register_access(uint16 hardwareID, uint8 instance,
	uint8 segment, uint32 registerIndex, amdgpu_register_access access)
{
	uint8 requested;
	switch (access) {
		case AMDGPU_REGISTER_ACCESS_READ:
			requested = AMDGPU_REGISTER_POLICY_READ;
			break;
		case AMDGPU_REGISTER_ACCESS_WRITE:
			requested = AMDGPU_REGISTER_POLICY_WRITE;
			break;
		default:
			return B_BAD_VALUE;
	}

	for (uint32 i = 0; i < sizeof(kVega10RegisterPolicy)
		/ sizeof(kVega10RegisterPolicy[0]); i++) {
		const vega10_register_policy& entry = kVega10RegisterPolicy[i];
		if (entry.hardware_id != hardwareID || entry.instance != instance
			|| entry.segment != segment
			|| entry.register_index != registerIndex) {
			continue;
		}
		return (entry.access_mask & requested) != 0
			? B_OK : B_NOT_ALLOWED;
	}

	return B_NOT_ALLOWED;
}
