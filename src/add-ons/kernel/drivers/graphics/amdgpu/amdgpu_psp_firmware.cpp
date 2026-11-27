/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <string.h>

#include "amdgpu_psp_firmware.h"

static uint16
ReadLE16(const uint8* data)
{
	return (uint16)data[0] | ((uint16)data[1] << 8);
}


static uint32
ReadLE32(const uint8* data)
{
	return (uint32)data[0] | ((uint32)data[1] << 8)
		| ((uint32)data[2] << 16) | ((uint32)data[3] << 24);
}


status_t
amdgpu_parse_vega10_psp_firmware(const void* data, size_t size,
	amdgpu_psp_firmware_info* info)
{
	/*
	 * common_firmware_header is 32 bytes; PSP v1.0 adds one
	 * 12-byte psp_fw_legacy_bin_desc (fw_version, offset, size).
	 */
	static const size_t kCommonHeaderSize = 32;
	static const size_t kPSPv10HeaderSize = 44;
	static const uint16 kExpectedHeaderMajor = 1;
	static const uint16 kExpectedHeaderMinor = 0;
	static const uint16 kVega10MP0Major = 9;
	static const uint16 kVega10MP0Minor = 0;

	if (info == NULL)
		return B_BAD_VALUE;
	memset(info, 0, sizeof(*info));
	if (data == NULL || size < kPSPv10HeaderSize)
		return B_BAD_VALUE;

	const uint8* bytes = (const uint8*)data;
	const uint32 fileSize = ReadLE32(bytes);
	const uint32 headerSize = ReadLE32(bytes + 4);
	const uint16 headerMajor = ReadLE16(bytes + 8);
	const uint16 headerMinor = ReadLE16(bytes + 10);
	const uint16 ipMajor = ReadLE16(bytes + 12);
	const uint16 ipMinor = ReadLE16(bytes + 14);
	const uint32 firmwareVersion = ReadLE32(bytes + 16);
	const uint32 payloadSize = ReadLE32(bytes + 20);
	const uint32 payloadOffset = ReadLE32(bytes + 24);
	const uint32 sosFeatureVersion = ReadLE32(bytes + 32);
	const uint32 sosOffset = ReadLE32(bytes + 36);
	const uint32 sosSize = ReadLE32(bytes + 40);

	if (fileSize != size || headerSize < kPSPv10HeaderSize
		|| headerSize > fileSize || headerMajor != kExpectedHeaderMajor
		|| headerMinor != kExpectedHeaderMinor
		|| ipMajor != kVega10MP0Major || ipMinor != kVega10MP0Minor)
		return B_BAD_DATA;

	if (payloadSize == 0 || payloadOffset < headerSize
		|| payloadOffset > fileSize
		|| payloadSize > fileSize - payloadOffset)
		return B_BAD_DATA;

	/*
	 * In the PSP v1.0 legacy descriptor, the SYS driver occupies the
	 * payload prefix and SOS follows at sosOffset. Linux's PSP 3.1
	 * initialization uses these exact fields to derive the two segments.
	 */
	if (sosOffset == 0 || sosSize == 0 || sosOffset > payloadSize
		|| sosSize > payloadSize - sosOffset)
		return B_BAD_DATA;

	info->firmware_version = firmwareVersion;
	info->feature_version = sosFeatureVersion;
	info->payload_offset = payloadOffset;
	info->payload_size = payloadSize;
	info->sys_offset = 0;
	info->sys_size = sosOffset;
	info->sos_offset = sosOffset;
	info->sos_size = sosSize;
	info->ip_version_major = ipMajor;
	info->ip_version_minor = ipMinor;
	return B_OK;
}
