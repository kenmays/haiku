/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VEGA10_REG_ACCESS_H
#define AMDGPU_VEGA10_REG_ACCESS_H

#include <SupportDefs.h>

/* Access classes are deliberately explicit; unknown values are denied. */
enum amdgpu_register_access {
	AMDGPU_REGISTER_ACCESS_READ = 0,
	AMDGPU_REGISTER_ACCESS_WRITE = 1
};

/*
 * Return B_OK only for registers whose access semantics have been reviewed
 * for this ASIC profile. This is a policy check, not an MMIO operation.
 * The initial allowlist contains one side-effect-free configuration read.
 */
status_t amdgpu_vega10_check_register_access(uint16 hardwareID,
	uint8 instance, uint8 segment, uint32 registerIndex,
	amdgpu_register_access access);

#endif
