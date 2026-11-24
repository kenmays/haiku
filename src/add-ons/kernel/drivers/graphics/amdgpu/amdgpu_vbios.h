/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VBIOS_H
#define AMDGPU_VBIOS_H

#include <SupportDefs.h>

/*
 * Results from validating a PCI option-ROM image chain and its first AMD
 * ATOMBIOS image. All offsets are relative to the supplied ROM byte buffer.
 */
struct amdgpu_vbios_info {
	uint32 image_offset;
	uint32 image_size;
	uint32 pcir_offset;
	uint32 atom_header_offset;
	uint32 master_data_table_offset;
	uint16 atom_header_size;
	uint8 image_code_type;
	uint8 image_indicator;
	uint8 atom_format_revision;
	uint8 atom_content_revision;
	bool is_atom_bios;
};

/*
 * Validate a complete, caller-supplied PCI option ROM image chain. Each
 * image must have the 0x55aa signature, a bounded PCIR structure and a
 * nonzero 512-byte image length. The chain must terminate with the PCIR
 * last-image bit. Every image checksum must be zero modulo 256.
 *
 * The first image containing a valid ATOM/MOTA ROM header is returned.
 * The parser performs no allocation, I/O, PCI config access or MMIO.
 *
 * Returns B_OK, B_BAD_VALUE for malformed data, or B_ENTRY_NOT_FOUND when
 * the chain is valid but contains no supported ATOMBIOS image.
 */
status_t amdgpu_parse_vbios_image(const void* data, size_t dataSize,
	amdgpu_vbios_info* info);

#endif
