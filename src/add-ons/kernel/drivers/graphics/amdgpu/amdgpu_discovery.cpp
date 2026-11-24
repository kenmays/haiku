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

	info->version = 0;
	info->die_count = 0;
	info->ip_count = 0;
	info->blocks = NULL;
	info->block_capacity = 0;

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
	uint16 dieIDs[AMDGPU_IP_DISCOVERY_MAX_DIES];
	for (uint32 i = 0; i < dieCount; i++) {
		dieIDs[i] = ReadLE16(bytes + 14 + i * 4);
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
		if (dieID != dieIDs[die] || numIPs > AMDGPU_IP_DISCOVERY_MAX_IPS
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


namespace {

static uint16
CalculateChecksum(const uint8* data, uint32 size)
{
	uint16 checksum = 0;
	for (uint32 i = 0; i < size; i++)
		checksum = (uint16)(checksum + data[i]);
	return checksum;
}

struct discovery_table_info {
	uint16 offset;
	uint16 checksum;
	uint16 size;
};

} // namespace

status_t
amdgpu_parse_discovery_binary(const void* data, size_t dataSize,
	amdgpu_ip_block* blocks, uint32 blockCapacity,
	amdgpu_ip_discovery_info* info)
{
	static const uint32 kBinaryHeaderV1Size = 12 + 7 * 8;
	static const uint32 kBinaryHeaderV2FixedSize = 16;
	static const uint32 kTableInfoSize = 8;
	static const uint32 kIPDiscoveryTableID = 0;
	static const uint32 kIPDiscoveryHeaderSize = 80;

	if (data == NULL || info == NULL || (blockCapacity != 0 && blocks == NULL)
		|| dataSize < 12) {
		return B_BAD_VALUE;
	}

	info->version = 0;
	info->die_count = 0;
	info->ip_count = 0;
	info->blocks = NULL;
	info->block_capacity = 0;

	const uint8* bytes = (const uint8*)data;
	if (ReadLE32(bytes) != AMDGPU_DISCOVERY_BINARY_SIGNATURE)
		return B_BAD_VALUE;

	uint16 major = ReadLE16(bytes + 4);
	uint16 checksum = ReadLE16(bytes + 8);
	uint16 binarySize = ReadLE16(bytes + 10);
	uint32 tableCount;
	uint32 directoryOffset;
	uint32 directoryEnd;

	if (binarySize > dataSize || binarySize < 12)
		return B_BAD_VALUE;

	if (major == 1) {
		if (binarySize < kBinaryHeaderV1Size)
			return B_BAD_VALUE;
		tableCount = 7;
		directoryOffset = 12;
		directoryEnd = kBinaryHeaderV1Size;
	} else if (major == 2) {
		if (binarySize < kBinaryHeaderV2FixedSize)
			return B_BAD_VALUE;
		tableCount = ReadLE16(bytes + 12);
		directoryOffset = kBinaryHeaderV2FixedSize;
		if (tableCount == 0 || tableCount > AMDGPU_DISCOVERY_MAX_TABLES
			|| tableCount > (binarySize - directoryOffset) / kTableInfoSize) {
			return B_BAD_VALUE;
		}
		directoryEnd = directoryOffset + tableCount * kTableInfoSize;
	} else {
		return B_BAD_VALUE;
	}

	if (directoryEnd > binarySize || directoryEnd < directoryOffset)
		return B_BAD_VALUE;

	// AMD's binary checksum is the 16-bit byte sum from immediately after
	// binary_checksum through the end of binary_size bytes.
	if (CalculateChecksum(bytes + 10, binarySize - 10) != checksum)
		return B_BAD_VALUE;

	discovery_table_info tables[AMDGPU_DISCOVERY_MAX_TABLES];
	for (uint32 i = 0; i < tableCount; i++) {
		const uint8* entry = bytes + directoryOffset + i * kTableInfoSize;
		tables[i].offset = ReadLE16(entry);
		tables[i].checksum = ReadLE16(entry + 2);
		tables[i].size = ReadLE16(entry + 4);

		if (tables[i].offset == 0 && tables[i].size == 0)
			continue;
		if (tables[i].offset == 0 || tables[i].size == 0
			|| tables[i].offset < directoryEnd
			|| tables[i].offset > binarySize
			|| tables[i].size > binarySize - tables[i].offset) {
			return B_BAD_VALUE;
		}

		uint32 tableEnd = tables[i].offset + tables[i].size;
		for (uint32 j = 0; j < i; j++) {
			if (tables[j].offset == 0)
				continue;
			uint32 otherEnd = tables[j].offset + tables[j].size;
			if (tables[i].offset < otherEnd && tables[j].offset < tableEnd)
				return B_BAD_VALUE;
		}
	}

	if (kIPDiscoveryTableID >= tableCount)
		return B_BAD_VALUE;

	const discovery_table_info& ipTable = tables[kIPDiscoveryTableID];
	if (ipTable.offset == 0 || ipTable.size < kIPDiscoveryHeaderSize)
		return B_BAD_VALUE;

	const uint8* table = bytes + ipTable.offset;
	if (ReadLE32(table) != AMDGPU_IP_DISCOVERY_SIGNATURE
		|| ReadLE32(table + 8) != kIPDiscoveryTableID) {
		return B_BAD_VALUE;
	}

	uint16 innerSize = ReadLE16(table + 6);
	if (innerSize < kIPDiscoveryHeaderSize || innerSize > ipTable.size
		|| CalculateChecksum(table, innerSize) != ipTable.checksum) {
		return B_BAD_VALUE;
	}

	return amdgpu_parse_ip_discovery_table(table, innerSize, blocks,
		blockCapacity, info);
}
