/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_DISCOVERY_H
#define AMDGPU_DISCOVERY_H

#include <SupportDefs.h>

static const uint32 AMDGPU_IP_DISCOVERY_SIGNATURE = 0x53445049;
static const uint32 AMDGPU_DISCOVERY_BINARY_SIGNATURE = 0x28211407;
static const uint32 AMDGPU_DISCOVERY_MAX_TABLES = 64;
static const uint32 AMDGPU_DISCOVERY_TABLE_IP = 0;
static const uint32 AMDGPU_IP_DISCOVERY_MAX_DIES = 16;
static const uint32 AMDGPU_IP_DISCOVERY_MAX_IPS = 256;
static const uint32 AMDGPU_IP_DISCOVERY_MAX_BASE_ADDRESSES = 16;

struct amdgpu_ip_block {
	uint16 hardware_id;
	uint8 instance;
	uint8 major;
	uint8 minor;
	uint8 revision;
	uint8 sub_revision;
	uint8 variant;
	uint8 base_address_count;
	uint64 base_addresses[AMDGPU_IP_DISCOVERY_MAX_BASE_ADDRESSES];
};

enum amdgpu_discovery_source_kind {
	AMDGPU_DISCOVERY_SOURCE_FIRMWARE = 1,
	AMDGPU_DISCOVERY_SOURCE_OPTION_ROM = 2
};

/*
 * Read exactly size bytes beginning at offset into buffer. A successful
 * callback must fill the entire buffer; short reads must return an error.
 * The source size is the bounded readable extent, not an inferred device BAR.
 */
typedef status_t (*amdgpu_discovery_read_at)(void* cookie, uint32 offset,
	void* buffer, size_t size);

struct amdgpu_discovery_source {
	amdgpu_discovery_source_kind kind;
	amdgpu_discovery_read_at read_at;
	void* cookie;
	size_t size;
};

struct amdgpu_ip_discovery_info {
	uint16 version;
	uint16 die_count;
	uint32 ip_count;
	struct amdgpu_ip_block* blocks;
	uint32 block_capacity;
};

/*
 * Parse a standalone AMD IP discovery table. The input is untrusted firmware
 * or ROM data: all offsets and variable-length records are bounds-checked.
 * The caller supplies storage; this routine performs no allocation and never
 * accesses hardware. Versions 1-4 are understood.
 *
 * Returns B_OK on success, B_BAD_VALUE for malformed/unsupported input, and
 * B_BUFFER_OVERFLOW when the output array is too small.
 */
status_t amdgpu_parse_ip_discovery_table(const void* data, size_t dataSize,
	amdgpu_ip_block* blocks, uint32 blockCapacity,
	amdgpu_ip_discovery_info* info);

/*
 * Validate an AMD discovery binary and parse its IP_DISCOVERY directory
 * entry. The binary and IP table checksums use AMD's 16-bit byte-sum format.
 * The output storage contract and status values match the standalone parser.
 */
status_t amdgpu_parse_discovery_binary(const void* data, size_t dataSize,
	amdgpu_ip_block* blocks, uint32 blockCapacity,
	amdgpu_ip_discovery_info* info);


/*
 * Read and parse a discovery binary from a firmware/ROM provider. The caller
 * supplies a scratch buffer at least as large as the declared binary. Reads
 * are bounded by source->size and the 16-bit binary length. This function
 * never maps BARs, probes hardware, allocates memory, or assumes a ROM layout.
 */
status_t amdgpu_read_discovery_source(
	const amdgpu_discovery_source* source, void* scratch, size_t scratchSize,
	amdgpu_ip_block* blocks, uint32 blockCapacity,
	amdgpu_ip_discovery_info* info);

#endif
