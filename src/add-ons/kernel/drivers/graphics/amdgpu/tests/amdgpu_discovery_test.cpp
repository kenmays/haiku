/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_discovery.h"

#include <stdio.h>
#include <string.h>
#include <vector>

static uint16
ReadLE16(const uint8* data)
{
	return (uint16)data[0] | ((uint16)data[1] << 8);
}

static void
WriteLE16(uint8* data, uint16 value)
{
	data[0] = value & 0xff;
	data[1] = value >> 8;
}

static void
WriteLE32(uint8* data, uint32 value)
{
	WriteLE16(data, value & 0xffff);
	WriteLE16(data + 2, value >> 16);
}

static void
WriteLE64(uint8* data, uint64 value)
{
	WriteLE32(data, (uint32)value);
	WriteLE32(data + 4, (uint32)(value >> 32));
}

static uint16
Checksum(const uint8* data, size_t size)
{
	uint16 sum = 0;
	for (size_t i = 0; i < size; i++)
		sum = (uint16)(sum + data[i]);
	return sum;
}

static std::vector<uint8>
MakeIPTable(uint16 version, bool address64)
{
	const uint32 addressSize = address64 ? 8 : 4;
	const uint32 tableSize = 80 + 4 + 8 + addressSize;
	std::vector<uint8> table(tableSize, 0);

	WriteLE32(&table[0], AMDGPU_IP_DISCOVERY_SIGNATURE);
	WriteLE16(&table[4], version);
	WriteLE16(&table[6], tableSize);
	WriteLE32(&table[8], AMDGPU_DISCOVERY_TABLE_IP);
	WriteLE16(&table[12], 1);
	WriteLE16(&table[14], 7);
	WriteLE16(&table[16], 80);
	if (address64)
		table[78] = 1;

	WriteLE16(&table[80], 7);
	WriteLE16(&table[82], 1);
	WriteLE16(&table[84], 0x1234);
	table[86] = 0;
	table[87] = 1;
	table[88] = 11;
	table[89] = 2;
	table[90] = 3;
	table[91] = version >= 3 ? 0x21 : 0;

	if (address64)
		WriteLE64(&table[92], 0x123456789abcdef0ULL);
	else
		WriteLE32(&table[92], 0x12345678);

	return table;
}

static std::vector<uint8>
MakeMultiDieIPTable()
{
	const uint32 tableSize = 112;
	std::vector<uint8> table(tableSize, 0);
	WriteLE32(&table[0], AMDGPU_IP_DISCOVERY_SIGNATURE);
	WriteLE16(&table[4], 3);
	WriteLE16(&table[6], tableSize);
	WriteLE32(&table[8], AMDGPU_DISCOVERY_TABLE_IP);
	WriteLE16(&table[12], 2);
	WriteLE16(&table[14], 7);
	WriteLE16(&table[16], 80);
	WriteLE16(&table[18], 9);
	WriteLE16(&table[20], 96);

	WriteLE16(&table[80], 7);
	WriteLE16(&table[82], 1);
	WriteLE16(&table[84], 0x100);
	table[87] = 1;
	table[88] = 10;
	WriteLE32(&table[92], 0x100000);

	WriteLE16(&table[96], 9);
	WriteLE16(&table[98], 1);
	WriteLE16(&table[100], 0x200);
	table[103] = 1;
	table[104] = 11;
	WriteLE32(&table[108], 0x200000);
	return table;
}

static std::vector<uint8>
MakeBinary(uint16 binaryVersion, bool address64 = false)
{
	std::vector<uint8> ipTable = MakeIPTable(address64 ? 4 : 3, address64);
	const uint32 directoryOffset = binaryVersion == 1 ? 12 : 16;
	const uint32 tableCount = 7;
	const uint32 directorySize = binaryVersion == 1 ? 56 : tableCount * 8;
	const uint32 tableOffset = directoryOffset + directorySize;
	const uint32 binarySize = tableOffset + ipTable.size();
	std::vector<uint8> binary(binarySize, 0);

	WriteLE32(&binary[0], AMDGPU_DISCOVERY_BINARY_SIGNATURE);
	WriteLE16(&binary[4], binaryVersion);
	WriteLE16(&binary[10], binarySize);
	if (binaryVersion == 2)
		WriteLE16(&binary[12], tableCount);

	uint8* entry = &binary[directoryOffset];
	WriteLE16(entry, tableOffset);
	WriteLE16(entry + 2, Checksum(&ipTable[0], ipTable.size()));
	WriteLE16(entry + 4, ipTable.size());
	memcpy(&binary[tableOffset], &ipTable[0], ipTable.size());
	WriteLE16(&binary[8], Checksum(&binary[10], binarySize - 10));
	return binary;
}

static status_t
ReadVector(void* cookie, uint32 offset, void* buffer, size_t size)
{
	std::vector<uint8>* bytes = (std::vector<uint8>*)cookie;
	if (offset > bytes->size() || size > bytes->size() - offset)
		return B_BAD_VALUE;
	memcpy(buffer, &(*bytes)[offset], size);
	return B_OK;
}

static bool
Check(bool condition, const char* name)
{
	if (!condition)
		fprintf(stderr, "FAIL: %s\n", name);
	return condition;
}

int
main()
{
	bool success = true;
	amdgpu_ip_block blocks[4];
	amdgpu_ip_discovery_info info;

	std::vector<uint8> multiDie = MakeMultiDieIPTable();
	success &= Check(amdgpu_parse_ip_discovery_table(&multiDie[0],
		multiDie.size(), blocks, 4, &info) == B_OK, "multi-die IP table");
	success &= Check(info.die_count == 2 && info.ip_count == 2
		&& blocks[1].hardware_id == 0x200, "multi-die records decoded");

	std::vector<uint8> binaryV1 = MakeBinary(1);

	amdgpu_discovery_source source;
	source.kind = AMDGPU_DISCOVERY_SOURCE_FIRMWARE;
	source.read_at = ReadVector;
	source.cookie = &binaryV1;
	source.size = binaryV1.size();
	uint8 sourceScratch[512];
	success &= Check(amdgpu_read_discovery_source(&source, sourceScratch,
		sizeof(sourceScratch), blocks, 4, &info) == B_OK,
		"read discovery binary from firmware source");
	success &= Check(info.ip_count == 1 && blocks[0].hardware_id == 0x1234,
		"firmware source decoded");

	source.kind = AMDGPU_DISCOVERY_SOURCE_OPTION_ROM;
	success &= Check(amdgpu_read_discovery_source(&source, sourceScratch,
		sizeof(sourceScratch), blocks, 4, &info) == B_OK,
		"read discovery binary from option ROM source");
	success &= Check(amdgpu_read_discovery_source(&source, sourceScratch,
		12, blocks, 4, &info) == B_BUFFER_OVERFLOW,
		"source scratch capacity checked");

	source.size--;
	success &= Check(amdgpu_read_discovery_source(&source, sourceScratch,
		sizeof(sourceScratch), blocks, 4, &info) == B_BAD_VALUE,
		"source extent checked");
	source.size++;

	std::vector<uint8> truncatedSource(binaryV1.begin(), binaryV1.end() - 1);
	source.cookie = &truncatedSource;
	success &= Check(amdgpu_read_discovery_source(&source, sourceScratch,
		sizeof(sourceScratch), blocks, 4, &info) == B_BAD_VALUE,
		"provider short read propagated");
	source.cookie = &binaryV1;

	source.kind = (amdgpu_discovery_source_kind)99;
	success &= Check(amdgpu_read_discovery_source(&source, sourceScratch,
		sizeof(sourceScratch), blocks, 4, &info) == B_BAD_VALUE,
		"unknown source kind rejected");
	source.kind = AMDGPU_DISCOVERY_SOURCE_OPTION_ROM;
	success &= Check(amdgpu_parse_discovery_binary(&binaryV1[0],
		binaryV1.size(), blocks, 4, &info) == B_OK, "valid v1 binary");
	success &= Check(info.version == 3 && info.die_count == 1
		&& info.ip_count == 1, "v1 parsed counts");
	success &= Check(blocks[0].hardware_id == 0x1234
		&& blocks[0].base_addresses[0] == 0x12345678,
		"v1 decoded IP record");

	std::vector<uint8> binaryV2 = MakeBinary(2);
	success &= Check(amdgpu_parse_discovery_binary(&binaryV2[0],
		binaryV2.size(), blocks, 4, &info) == B_OK, "valid v2 binary");

	std::vector<uint8> binary64 = MakeBinary(2, true);
	success &= Check(amdgpu_parse_discovery_binary(&binary64[0],
		binary64.size(), blocks, 4, &info) == B_OK, "valid 64-bit base address");
	success &= Check(blocks[0].base_addresses[0] == 0x123456789abcdef0ULL,
		"decoded 64-bit base address");

	success &= Check(amdgpu_parse_discovery_binary(&binaryV1[0],
		binaryV1.size(), blocks, 0, &info) == B_BUFFER_OVERFLOW,
		"output capacity checked");

	std::vector<uint8> badTableChecksum(binaryV1);
	uint32 checksumTableOffset = ReadLE16(&badTableChecksum[12]);
	badTableChecksum[checksumTableOffset + 90] ^= 1;
	WriteLE16(&badTableChecksum[8], Checksum(&badTableChecksum[10],
		badTableChecksum.size() - 10));
	success &= Check(amdgpu_parse_discovery_binary(&badTableChecksum[0],
		badTableChecksum.size(), blocks, 4, &info) == B_BAD_VALUE,
		"IP table checksum corruption rejected");

	std::vector<uint8> badChecksum(binaryV1);
	badChecksum.back() ^= 1;
	success &= Check(amdgpu_parse_discovery_binary(&badChecksum[0],
		badChecksum.size(), blocks, 4, &info) == B_BAD_VALUE,
		"binary checksum corruption rejected");

	success &= Check(amdgpu_parse_discovery_binary(&binaryV1[0],
		binaryV1.size() - 1, blocks, 4, &info) == B_BAD_VALUE,
		"truncated binary rejected");

	std::vector<uint8> badVersion(binaryV1);
	WriteLE16(&badVersion[4], 0);
	WriteLE16(&badVersion[8], Checksum(&badVersion[10], badVersion.size() - 10));
	success &= Check(amdgpu_parse_discovery_binary(&badVersion[0],
		badVersion.size(), blocks, 4, &info) == B_BAD_VALUE,
		"unsupported binary version rejected");

	std::vector<uint8> badDirectory(binaryV2);
	WriteLE16(&badDirectory[16], 20);
	WriteLE16(&badDirectory[8], Checksum(&badDirectory[10],
		badDirectory.size() - 10));
	success &= Check(amdgpu_parse_discovery_binary(&badDirectory[0],
		badDirectory.size(), blocks, 4, &info) == B_BAD_VALUE,
		"directory pointing into header rejected");

	std::vector<uint8> overlappingTables(binaryV1);
	uint32 firstTableOffset = ReadLE16(&overlappingTables[12]);
	WriteLE16(&overlappingTables[20], firstTableOffset + 1);
	WriteLE16(&overlappingTables[22], 0);
	WriteLE16(&overlappingTables[24], 4);
	WriteLE16(&overlappingTables[8], Checksum(&overlappingTables[10],
		overlappingTables.size() - 10));
	success &= Check(amdgpu_parse_discovery_binary(&overlappingTables[0],
		overlappingTables.size(), blocks, 4, &info) == B_BAD_VALUE,
		"overlapping table entries rejected");

	std::vector<uint8> badDie(binaryV1);
	uint32 tableOffset = ReadLE16(&badDie[12]);
	WriteLE16(&badDie[tableOffset + 80], 8);
	WriteLE16(&badDie[14], Checksum(&badDie[tableOffset],
		ReadLE16(&badDie[tableOffset + 6])));
	WriteLE16(&badDie[8], Checksum(&badDie[10], badDie.size() - 10));
	success &= Check(amdgpu_parse_discovery_binary(&badDie[0],
		badDie.size(), blocks, 4, &info) == B_BAD_VALUE,
		"die ID mismatch rejected");

	if (!success)
		return 1;

	printf("AMDGPU discovery parser tests passed.\n");
	return 0;
}
