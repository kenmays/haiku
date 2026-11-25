/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <SupportDefs.h>

#include "amdgpu_vega10_registers.h"


struct vega10_register_base {
	uint16 hardware_id;
	uint32 segment[AMDGPU_VEGA10_MAX_REGISTER_SEGMENTS];
};


/*
 * Transcribed from upstream Linux:
 * drivers/gpu/drm/amd/include/vega10_ip_offset.h
 *
 * The first dimension is the logical IP instance; the second is the
 * register BASE_IDX segment. The active vega10_reg_base_init() path points
 * at these IP_BASE.instance[] arrays. All currently supported instances
 * beyond instance zero are zero/unimplemented in that upstream table.
 *
 * Keep these in register-index (dword) units. Do not multiply these values
 * or combine them with IP-discovery base_addresses when populating this
 * table.
 */
static const vega10_register_base kVega10RegisterBases[] = {
	{108, {0x00000000, 0x00000014, 0x00000d20, 0x00010400, 0}},
	{255, {0x00016000, 0, 0, 0, 0}},
	{46,  {0x00007000, 0, 0, 0, 0}},
	{12,  {0x00007800, 0x00007e00, 0, 0, 0}},
	{35,  {0x00000c20, 0, 0, 0, 0}},
	{32,  {0x00007e00, 0x00048800, 0, 0, 0}},
	{11,  {0x00002000, 0x0000a000, 0, 0, 0}},
	{34,  {0x0001a000, 0, 0, 0, 0}},
	{41,  {0x00000f20, 0, 0, 0, 0}},
	{40,  {0x000010a0, 0, 0, 0, 0}},
	{42,  {0x00001260, 0, 0, 0, 0}},
	{43,  {0x00001460, 0, 0, 0, 0}},
	{150, {0x00014000, 0, 0, 0, 0}},
	{3,   {0x00016600, 0, 0, 0, 0}},
	{4,   {0x00016800, 0, 0, 0, 0}},
	{10,  {0x00016a00, 0, 0, 0, 0}}
};


status_t
amdgpu_vega10_register_base(uint16 hardwareID, uint8 instance,
	uint8 segment, uint32* baseDword)
{
	if (baseDword == NULL || instance >= AMDGPU_VEGA10_MAX_REGISTER_INSTANCES
		|| segment >= AMDGPU_VEGA10_MAX_REGISTER_SEGMENTS) {
		return B_BAD_VALUE;
	}

	for (uint32 i = 0; i < sizeof(kVega10RegisterBases)
		/ sizeof(kVega10RegisterBases[0]); i++) {
		if (kVega10RegisterBases[i].hardware_id != hardwareID)
			continue;

		/* Instance zero is the only populated instance in this profile. */
		if (instance != 0)
			return B_NOT_SUPPORTED;

		uint32 base = kVega10RegisterBases[i].segment[segment];
		/*
		 * A zero base is valid for NBIO segment zero. For all other
		 * entries, zero means the segment is not implemented.
		 */
		if (base == 0 && !(hardwareID == 108 && segment == 0))
			return B_NOT_SUPPORTED;

		*baseDword = base;
		return B_OK;
	}

	return B_NOT_SUPPORTED;
}


status_t
amdgpu_vega10_register_byte_offset(uint16 hardwareID, uint8 instance,
	uint8 segment, uint32 registerIndex, uint32* byteOffset)
{
	if (byteOffset == NULL)
		return B_BAD_VALUE;

	uint32 baseDword;
	status_t status = amdgpu_vega10_register_base(hardwareID, instance,
		segment, &baseDword);
	if (status != B_OK)
		return status;

	/* Check addition before converting the SOC15 dword index to bytes. */
	if (registerIndex > 0xffffffffU - baseDword)
		return B_BAD_VALUE;

	uint32 registerDword = baseDword + registerIndex;
	if (registerDword > 0xffffffffU / sizeof(uint32))
		return B_BAD_VALUE;

	*byteOffset = registerDword * sizeof(uint32);
	return B_OK;
}
