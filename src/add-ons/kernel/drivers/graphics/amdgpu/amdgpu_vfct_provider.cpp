/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_vfct_provider.h"

#include <ACPI.h>

#include <string.h>

static const uint32 kMaximumVFCTTableSize = 16 * 1024 * 1024;


static uint32
ReadLE32(const uint8* data)
{
	return (uint32)data[0] | ((uint32)data[1] << 8)
		| ((uint32)data[2] << 16) | ((uint32)data[3] << 24);
}


status_t
amdgpu_get_vbios_from_vfct(acpi_table_module_info* acpi,
	const amdgpu_vfct_device* device, void* biosBuffer,
	size_t biosCapacity, size_t* biosSize, amdgpu_vfct_info* info)
{
	if (biosSize != NULL)
		*biosSize = 0;
	if (info != NULL)
		memset(info, 0, sizeof(*info));

	if (acpi == NULL || device == NULL || biosSize == NULL || info == NULL)
		return B_BAD_VALUE;
	if (acpi->get_table == NULL || acpi->put_table == NULL)
		return B_NOT_SUPPORTED;

	void* table = NULL;
	status_t status = acpi->get_table("VFCT", 1, &table);
	if (status != B_OK)
		return status;
	if (table == NULL) {
		/*
		 * get_table() succeeded, so still pair it with put_table(). A null
		 * handle is a broken provider result, but must not skip cleanup.
		 */
		status = B_BAD_VALUE;
	} else {
		/*
		 * ACPICA owns the mapped table. Keep its reference until parsing and
		 * the image copy have completed; no pointer into it escapes.
		 */
		const uint8* bytes = (const uint8*)table;
		const uint32 tableSize = ReadLE32(bytes + 4);
		if (tableSize < 76 || tableSize > kMaximumVFCTTableSize) {
			status = B_BAD_VALUE;
		} else {
		amdgpu_vfct_info parsed;
		status = amdgpu_parse_vfct_table(table, tableSize, device, &parsed);
		if (status == B_OK) {
			*biosSize = parsed.image_size;
			if (biosBuffer == NULL && biosCapacity == 0) {
				*info = parsed;
				status = B_BUFFER_OVERFLOW;
			} else if (biosBuffer == NULL) {
				status = B_BAD_VALUE;
			} else if (biosCapacity < parsed.image_size) {
				status = B_BUFFER_OVERFLOW;
			} else {
				memcpy(biosBuffer, bytes + parsed.image_offset,
					parsed.image_size);
				*info = parsed;
			}
		}
		}
	}

	status_t releaseStatus = acpi->put_table(table);
	if (releaseStatus != B_OK)
		return releaseStatus;

	return status;
}



status_t
amdgpu_query_vbios_size_from_vfct(acpi_table_module_info* acpi,
	const amdgpu_vfct_device* device, size_t* biosSize,
	amdgpu_vfct_info* info)
{
	return amdgpu_get_vbios_from_vfct(acpi, device, NULL, 0, biosSize,
		info);
}
