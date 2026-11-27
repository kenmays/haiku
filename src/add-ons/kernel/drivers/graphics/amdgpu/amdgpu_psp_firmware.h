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

/*
 * Explicit caller-selected compatibility bounds. The project does not yet
 * ship authoritative minimum versions for every Vega 10 board/firmware set;
 * callers must populate these from a validated platform policy.
 */
struct amdgpu_psp_firmware_version_policy {
	uint32 sos_firmware_min;
	uint32 sos_firmware_max;
	uint32 sos_feature_min;
	uint32 sos_feature_max;
	uint32 asd_firmware_min;
	uint32 asd_firmware_max;
	uint32 asd_feature_min;
	uint32 asd_feature_max;
};

/* Borrowed firmware views; source buffers remain owned by the caller. */
struct amdgpu_psp_firmware_segment {
	const uint8* data;
	size_t size;
	uint32 firmware_version;
	uint32 feature_version;
};

static const uint32 AMDGPU_PSP_FIRMWARE_BUNDLE_MAGIC = 0x41505342;

struct amdgpu_psp_firmware_bundle {
	uint32 magic;
	const void* sos_container;
	size_t sos_container_size;
	const void* asd_container;
	size_t asd_container_size;
	amdgpu_psp_firmware_segment sys;
	amdgpu_psp_firmware_segment sos;
	amdgpu_psp_firmware_segment asd;
	bool loaded;
};

void amdgpu_psp_firmware_bundle_init(amdgpu_psp_firmware_bundle* bundle);
status_t amdgpu_psp_firmware_bundle_load(
	amdgpu_psp_firmware_bundle* bundle, const void* sos_data,
	size_t sos_size, const void* asd_data, size_t asd_size);
status_t amdgpu_psp_firmware_bundle_unload(
	amdgpu_psp_firmware_bundle* bundle);
status_t amdgpu_psp_firmware_bundle_check_versions(
	const amdgpu_psp_firmware_bundle* bundle,
	const amdgpu_psp_firmware_version_policy* policy);

#endif
