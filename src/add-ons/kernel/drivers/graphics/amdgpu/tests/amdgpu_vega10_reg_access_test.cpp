/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_vega10_reg_access.h"
#include "../amdgpu_vega10_mmio.h"

#include <stdio.h>


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
	bool success = true;
	success &= Check(amdgpu_vega10_check_register_access(11, 0, 0,
		AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX,
		AMDGPU_REGISTER_ACCESS_READ) == B_OK,
		"GB_ADDR_CONFIG read is allowlisted");
	success &= Check(amdgpu_vega10_check_register_access(11, 0, 0,
		AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX,
		AMDGPU_REGISTER_ACCESS_WRITE) == B_NOT_ALLOWED,
		"GB_ADDR_CONFIG write is denied");
	success &= Check(amdgpu_vega10_check_register_access(11, 0, 0,
		AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX + 1,
		AMDGPU_REGISTER_ACCESS_READ) == B_NOT_ALLOWED,
		"adjacent register is denied by default");
	success &= Check(amdgpu_vega10_check_register_access(11, 1, 0,
		AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX,
		AMDGPU_REGISTER_ACCESS_READ) == B_NOT_ALLOWED,
		"unreviewed IP instance is denied");
	success &= Check(amdgpu_vega10_check_register_access(11, 0, 1,
		AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX,
		AMDGPU_REGISTER_ACCESS_READ) == B_NOT_ALLOWED,
		"unreviewed register segment is denied");
	success &= Check(amdgpu_vega10_check_register_access(42, 0, 0,
		AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX,
		AMDGPU_REGISTER_ACCESS_READ) == B_NOT_ALLOWED,
		"other IP block is denied");
	success &= Check(amdgpu_vega10_check_register_access(11, 0, 0,
		AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX,
		(amdgpu_register_access)99) == B_BAD_VALUE,
		"unknown access type is rejected");

	if (!success)
		return 1;

	printf("AMDGPU Vega 10 register access policy tests passed.\n");
	return 0;
}
