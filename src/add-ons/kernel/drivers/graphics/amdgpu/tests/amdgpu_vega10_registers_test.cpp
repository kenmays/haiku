/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_vega10_registers.h"

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
	uint32 base = 0;
	uint32 offset = 0;

	success &= Check(amdgpu_vega10_register_base(11, 0, 0, &base) == B_OK
		&& base == 0x00002000,
		"GC instance zero segment zero uses upstream base");
	success &= Check(amdgpu_vega10_register_base(11, 0, 1, &base) == B_OK
		&& base == 0x0000a000,
		"GC segment one uses upstream base");
	success &= Check(amdgpu_vega10_register_base(108, 0, 0, &base) == B_OK
		&& base == 0,
		"zero-valued NBIO segment zero is a valid base");
	success &= Check(amdgpu_vega10_register_base(108, 0, 3, &base) == B_OK
		&& base == 0x00010400,
		"NBIO segment three uses upstream base");
	success &= Check(amdgpu_vega10_register_base(42, 0, 0, &base) == B_OK
		&& base == 0x00001260,
		"SDMA0 base is available");
	success &= Check(amdgpu_vega10_register_base(43, 0, 0, &base) == B_OK
		&& base == 0x00001460,
		"SDMA1 base is available");
	success &= Check(amdgpu_vega10_register_base(150, 0, 0, &base) == B_OK
		&& base == 0x00014000,
		"UMC base is available");

	success &= Check(amdgpu_vega10_register_byte_offset(11, 0, 0,
		0x10, &offset) == B_OK && offset == 0x8040,
		"SOC15 dword index converts to byte offset with checked arithmetic");
	success &= Check(amdgpu_vega10_register_byte_offset(108, 0, 0,
		1, &offset) == B_OK && offset == 4,
		"zero-base NBIO register index converts correctly");

	success &= Check(amdgpu_vega10_register_base(11, 1, 0, &base)
		== B_NOT_SUPPORTED,
		"unpopulated IP instance is rejected");
	success &= Check(amdgpu_vega10_register_base(11, 0, 2, &base)
		== B_NOT_SUPPORTED,
		"unpopulated register segment is rejected");
	success &= Check(amdgpu_vega10_register_base(999, 0, 0, &base)
		== B_NOT_SUPPORTED,
		"unknown IP hardware ID is rejected");
	success &= Check(amdgpu_vega10_register_base(1, 0, 0, &base)
		== B_NOT_SUPPORTED,
		"MP1 is withheld because upstream static and generated bases disagree");
	success &= Check(amdgpu_vega10_register_base(11, 5, 0, &base)
		== B_BAD_VALUE,
		"instance outside table bounds is rejected");
	success &= Check(amdgpu_vega10_register_base(11, 0, 5, &base)
		== B_BAD_VALUE,
		"segment outside table bounds is rejected");
	success &= Check(amdgpu_vega10_register_base(11, 0, 0, NULL)
		== B_BAD_VALUE,
		"null base output is rejected");
	success &= Check(amdgpu_vega10_register_byte_offset(11, 0, 0,
		0xffffffffU, &offset) == B_BAD_VALUE,
		"dword addition overflow is rejected");
	success &= Check(amdgpu_vega10_register_byte_offset(11, 0, 0,
		0x3fffffffU, &offset) == B_BAD_VALUE,
		"byte-offset multiplication overflow is rejected");
	success &= Check(amdgpu_vega10_register_byte_offset(11, 0, 0,
		0, NULL) == B_BAD_VALUE,
		"null byte-offset output is rejected");

	if (!success)
		return 1;

	printf("AMDGPU Vega 10 register-base tests passed.\n");
	return 0;
}
