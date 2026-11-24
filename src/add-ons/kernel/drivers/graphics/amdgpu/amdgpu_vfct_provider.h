/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VFCT_PROVIDER_H
#define AMDGPU_VFCT_PROVIDER_H

#include <SupportDefs.h>

#include "amdgpu_vfct.h"

struct acpi_table_module_info;

/*
 * Fetch and validate VFCT VBIOS data while holding the ACPICA table
 * reference. The selected image is copied to caller-owned storage before
 * put_table() releases the reference.
 *
 * The caller must acquire the table module with get_module() and release
 * it with put_module() after this call. biosSize is set to the required
 * image size on B_BUFFER_OVERFLOW. info contains VFCT metadata; its offsets
 * refer to the original ACPI table and are not offsets into biosBuffer.
 */
status_t amdgpu_get_vbios_from_vfct(struct acpi_table_module_info* acpi,
	const amdgpu_vfct_device* device, void* biosBuffer,
	size_t biosCapacity, size_t* biosSize, amdgpu_vfct_info* info);

#endif
