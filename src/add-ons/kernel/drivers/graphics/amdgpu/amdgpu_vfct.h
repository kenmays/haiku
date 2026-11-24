/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VFCT_H
#define AMDGPU_VFCT_H

#include <SupportDefs.h>

#include "amdgpu_vbios.h"

/*
 * PCI identity recorded by the platform firmware in an ACPI VFCT VBIOS
 * entry. Bus mismatches are reported but accepted: firmware records the
 * POST-time bus number, which may differ after PCI bus renumbering.
 */
struct amdgpu_vfct_device {
	uint32 bus;
	uint32 device;
	uint32 function;
	uint16 vendor_id;
	uint16 device_id;
};

struct amdgpu_vfct_info {
	uint32 image_offset;
	uint32 image_size;
	uint32 entry_offset;
	uint32 bus;
	uint32 device;
	uint32 function;
	uint16 vendor_id;
	uint16 device_id;
	uint16 subsystem_vendor_id;
	uint16 subsystem_device_id;
	uint32 revision;
	bool bus_matches;
	amdgpu_vbios_info vbios;
};

/*
 * Parse an entire ACPI VFCT table and select an ATOMBIOS image matching
 * vendor/device and PCI device/function. A recorded bus mismatch is
 * accepted and exposed through info->bus_matches, matching Linux behavior.
 *
 * The ACPI table checksum, declared length, VBIOS offset, each image-entry
 * header and image extent are validated. The embedded option ROM is passed
 * through amdgpu_parse_vbios_image() before it is returned.
 *
 * image_offset is relative to the start of the supplied ACPI table.
 * No allocation, ACPI calls, PCI access, or MMIO is performed.
 *
 * Returns B_OK, B_BAD_VALUE for malformed data, or B_ENTRY_NOT_FOUND when
 * no matching AMD ATOMBIOS entry exists.
 */
status_t amdgpu_parse_vfct_table(const void* data, size_t dataSize,
	const amdgpu_vfct_device* device, amdgpu_vfct_info* info);

#endif
