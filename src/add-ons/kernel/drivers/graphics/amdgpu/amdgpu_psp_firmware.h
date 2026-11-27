/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_PSP_FIRMWARE_H
#define AMDGPU_PSP_FIRMWARE_H

#include <SupportDefs.h>

/*
 * Metadata extracted from the AMD PSP legacy firmware v1.0 container used by
 * the Vega 10 PSP 3.1 path. Offsets are relative to the container payload.
 * This parser does not authenticate, load, or execute firmware.
 */
struct amdgpu_psp_firmware_info {
	uint32 firmware_version;
	uint32 feature_version;
	uint32 payload_offset;
	uint32 payload_size;
	uint32 sys_offset;
	uint32 sys_size;
	uint32 sos_offset;
	uint32 sos_size;
	uint16 ip_version_major;
	uint16 ip_version_minor;
};

status_t amdgpu_parse_vega10_psp_firmware(const void* data, size_t size,
	amdgpu_psp_firmware_info* info);

/*
 * ASD uses a common_firmware_header followed by one contiguous microcode
 * payload. Its descriptor is not the SOS legacy segment descriptor.
 */
status_t amdgpu_parse_vega10_asd_firmware(const void* data, size_t size,
	amdgpu_psp_firmware_info* info);

#endif
