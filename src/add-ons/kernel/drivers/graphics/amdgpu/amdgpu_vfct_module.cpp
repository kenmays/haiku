/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_vfct_provider.h"

#include <ACPI.h>
#include <KernelExport.h>


status_t
amdgpu_load_vbios_from_vfct(const amdgpu_vfct_device* device,
	void* biosBuffer, size_t biosCapacity, size_t* biosSize,
	amdgpu_vfct_info* info)
{
	acpi_table_module_info* acpi = NULL;
	status_t status = get_module(B_ACPI_TABLE_MODULE_NAME,
		(module_info**)&acpi);
	if (status != B_OK)
		return status;

	status = amdgpu_get_vbios_from_vfct(acpi, device, biosBuffer,
		biosCapacity, biosSize, info);
	put_module(B_ACPI_TABLE_MODULE_NAME);
	return status;
}
