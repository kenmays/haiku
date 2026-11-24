/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_discovery.h"

#include <string.h>

namespace {

static const uint32 kDiscoveryHeaderSize = 80;
static const uint32 kDieHeaderSize = 4;
static const uint32 kIPRecordHeaderSize = 8;

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

static uint64
ReadLE64(const uint8* data)
{
	return (uint64)ReadLE32(data) | ((uint64)ReadLE32(data + 4) << 32);
}

} // namespace

status_t
amdgpu_parse_ip_discovery_table(const void* data, size_t dataSize,
	amdgpu_ip_block* blocks, uint32 blockCapacity,
	amdgpu_ip_discovery_info* info)
{
	if (data == NULL || info == NULL || (blockCapacity != 0 && blocks == NULL)
		|| dataSize < kDiscoveryHeaderSize) {
		return B_BAD_VALUE;
	}

	const uint8* bytes = (const uint8*)data;
	if (ReadLE32(bytes) != AMDGPU_IP_DISCOVERY_SIGNATURE
		|| ReadLE32(bytes + 8) != 0 /* IP_DISCOVERY table ID */) {
		return B_BAD_VALUE;
	}

	uint16 version = ReadLE16(bytes + 4);
	uint16 tableSize = ReadLE16(bytes + 6);
	uint16 dieCount = ReadLE16(bytes + 12);
	if (version < 1 || version > 4 || tableSize < kDiscoveryHeaderSize
		|| tableSize > dataSize || dieCount > AMDGPU_IP_DISCOVERY_MAX_DIES) {
		return B_BAD_VALUE;
	}

	uint16 dieOffsets[AMDGPU_IP_DISCOVERY_MAX_DIES];
	for (uint32 i = 0; i < dieCount; i++) {
		dieOffsets[i] = ReadLE16(bytes + 14 + i * 4 + 2);
		if (dieOffsets[i] < kDiscoveryHeaderSize
			|| dieOffsets[i] > tableSize - kDieHeaderSize) {
			return B_BAD_VALUE;
		}
		for (uint32 j = 0; j < i; j++) {
			if (dieOffsets[i] == dieOffsets[j])
				return B_BAD_VALUE;
		}
	}

	bool addresses64 = version == 4 && (bytes[78] & 0x01) != 0;
	uint32 totalIPs = 0;
	uint32 outputCount = 0;
	for (uint32 die = 0; die < dieCount; die++) {
		uint32 dieOffset = dieOffsets[die];
		uint32 dieLimit = tableSize;
		for (uint32 other = 0; other < dieCount; other++) {
			if (dieOffsets[other] > dieOffset && dieOffsets[other] < dieLimit)
				dieLimit = dieOffsets[other];
		}
		if (dieLimit - dieOffset < kDieHeaderSize)
			return B_BAD_VALUE;

		uint16 dieID = ReadLE16(bytes + dieOffset);
		uint16 numIPs = ReadLE16(bytes + dieOffset + 2);
		if (dieID != die || numIPs > AMDGPU_IP_DISCOVERY_MAX_IPS
			|| totalIPs > AMDGPU_IP_DISCOVERY_MAX_IPS - numIPs) {
			return B_BAD_VALUE;
		}
		totalIPs += numIPs;

		uint32 offset = dieOffset + kDieHeaderSize;
		for (uint32 index = 0; index < numIPs; index++) {
			if (offset > dieLimit || dieLimit - offset < kIPRecordHeaderSize)
				return B_BAD_VALUE;

			const uint8* ip = bytes + offset;
			uint8 baseCount = ip[3];
			if (baseCount > AMDGPU_IP_DISCOVERY_MAX_BASE_ADDRESSES)
				return B_BAD_VALUE;

			uint32 addressSize = addresses64 ? 8 : 4;
			uint32 recordSize = kIPRecordHeaderSize
				+ (uint32)baseCount * addressSize;
			if (recordSize > dieLimit - offset)
				return B_BAD_VALUE;

			if (outputCount >= blockCapacity)
				return B_BUFFER_OVERFLOW;

			amdgpu_ip_block& block = blocks[outputCount++];
			memset(&block, 0, sizeof(block));
			block.hardware_id = ReadLE16(ip);
			block.instance = ip[2];
			block.major = ip[4];
			block.minor = ip[5];
			block.revision = ip[6];
			// Version 1-2 use the harvest/reserved nibble. Version 3-4
			// define sub-revision in the low nibble and variant in the high.
			if (version >= 3) {
				block.sub_revision = ip[7] & 0x0f;
				block.variant = ip[7] >> 4;
			}
			block.base_address_count = baseCount;

			for (uint32 address = 0; address < baseCount; address++) {
				const uint8* value = ip + kIPRecordHeaderSize
					+ address * addressSize;
				block.base_addresses[address] = addresses64
					? ReadLE64(value) : ReadLE32(value);
			}

			offset += recordSize;
		}
	}

	info->version = version;
	info->die_count = dieCount;
	info->ip_count = outputCount;
	info->blocks = blocks;
	info->block_capacity = blockCapacity;
	return B_OK;
}
