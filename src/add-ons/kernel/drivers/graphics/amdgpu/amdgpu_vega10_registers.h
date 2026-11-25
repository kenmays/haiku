/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VEGA10_REGISTERS_H
#define AMDGPU_VEGA10_REGISTERS_H

#include <SupportDefs.h>

/*
 * Vega 10 SOC15 register-base metadata.
 *
 * Values are register indices (dwords), matching the unit used by
 * SOC15_REG_OFFSET in upstream Linux. They are NOT BAR-relative byte
 * offsets, CPU physical addresses, or IP-discovery base_addresses.
 *
 * Only bases whose static table values agree with the active upstream
 * vega10_reg_base_init() path are exposed. In particular, MP1 is omitted:
 * the upstream static MP1_BASE initializer and generated MP1 macros disagree.
 */
static const uint8 AMDGPU_VEGA10_MAX_REGISTER_INSTANCES = 5;
static const uint8 AMDGPU_VEGA10_MAX_REGISTER_SEGMENTS = 5;

/* Hardware IDs are the values defined by upstream soc15_hw_ip.h. */
status_t amdgpu_vega10_register_base(uint16 hardwareID, uint8 instance,
	uint8 segment, uint32* baseDword);

/*
 * Convert an SOC15-style register index to a byte offset from the register
 * aperture's logical base. This is arithmetic only; it does not select a
 * BAR, resolve an ASIC register hole, map memory, or access MMIO.
 */
status_t amdgpu_vega10_register_byte_offset(uint16 hardwareID,
	uint8 instance, uint8 segment, uint32 registerIndex,
	uint32* byteOffset);

#endif
