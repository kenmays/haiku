/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_discovery.h"

#include <string.h>

namespace {

static uint16
ReadLE16(const uint8* data)
{
	return (uint16)data[0] | ((uint16)data[1] << 8);
}

static void
ClearInfo(amdgpu_ip_discovery_info* info)
{
	if (info == NULL)
		return;
	info->version = 0;
	info->die_count = 0;
	info->ip_count = 0;
	info->blocks = NULL;
	info->block_capacity = 0;
}

} // namespace

status_t
amdgpu_read_discovery_source(const amdgpu_discovery_source* source,
	void* scratch, size_t scratchSize, amdgpu_ip_block* blocks,
	uint32 blockCapacity, amdgpu_ip_discovery_info* info)
{
	ClearInfo(info);
	if (source == NULL || source->read_at == NULL || scratch == NULL
		|| info == NULL || (blockCapacity != 0 && blocks == NULL)
		|| source->size < 12
		|| (source->kind != AMDGPU_DISCOVERY_SOURCE_FIRMWARE
			&& source->kind != AMDGPU_DISCOVERY_SOURCE_OPTION_ROM)) {
		return B_BAD_VALUE;
	}

	if (scratchSize < 12)
		return B_BUFFER_OVERFLOW;

	status_t status = source->read_at(source->cookie, 0, scratch, 12);
	if (status != B_OK)
		return status;

	const uint8* header = (const uint8*)scratch;
	uint16 binarySize = ReadLE16(header + 10);
	if (binarySize < 12 || binarySize > source->size)
		return B_BAD_VALUE;
	if (binarySize > scratchSize)
		return B_BUFFER_OVERFLOW;

	if (binarySize > 12) {
		status = source->read_at(source->cookie, 12,
			(uint8*)scratch + 12, binarySize - 12);
		if (status != B_OK)
			return status;
	}

	return amdgpu_parse_discovery_binary(scratch, binarySize, blocks,
		blockCapacity, info);
}
