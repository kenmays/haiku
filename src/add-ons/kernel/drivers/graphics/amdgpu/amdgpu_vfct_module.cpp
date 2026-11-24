/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_vfct_provider.h"

#include <ACPI.h>
#include <KernelExport.h>

#include <string.h>
#include "pci_device.h"


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


status_t
amdgpu_load_vbios_for_pci_device(const amdgpu_pci_device* pciDevice,
	void* biosBuffer, size_t biosCapacity, size_t* biosSize,
	amdgpu_vfct_info* info)
{
	if (biosSize != NULL)
		*biosSize = 0;
	if (info != NULL)
		memset(info, 0, sizeof(*info));
	if (pciDevice == NULL || biosBuffer == NULL || biosSize == NULL
		|| info == NULL) {
		return B_BAD_VALUE;
	}

	amdgpu_vfct_device vfctDevice;
	status_t status = amdgpu_vfct_device_from_pci_device(pciDevice,
		&vfctDevice);
	if (status != B_OK)
		return status;

	return amdgpu_load_vbios_from_vfct(&vfctDevice, biosBuffer,
		biosCapacity, biosSize, info);
}


status_t
amdgpu_query_vbios_size_for_pci_device(const amdgpu_pci_device* pciDevice,
	size_t* biosSize, amdgpu_vfct_info* info)
{
	if (biosSize != NULL)
		*biosSize = 0;
	if (info != NULL)
		memset(info, 0, sizeof(*info));
	if (pciDevice == NULL || biosSize == NULL || info == NULL)
		return B_BAD_VALUE;

	amdgpu_vfct_device vfctDevice;
	status_t status = amdgpu_vfct_device_from_pci_device(pciDevice,
		&vfctDevice);
	if (status != B_OK)
		return status;

	acpi_table_module_info* acpi = NULL;
	status = get_module(B_ACPI_TABLE_MODULE_NAME, (module_info**)&acpi);
	if (status != B_OK)
		return status;

	status = amdgpu_query_vbios_size_from_vfct(acpi, &vfctDevice,
		biosSize, info);
	put_module(B_ACPI_TABLE_MODULE_NAME);
	return status;
}
