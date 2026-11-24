/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_vbios.h"

#include <string.h>

static uint16
ReadLE16(const uint8* data)
{
	return (uint16)data[0] | ((uint16)data[1] << 8);
}


static bool
RangeIsValid(size_t offset, size_t length, size_t limit)
{
	return offset <= limit && length <= limit - offset;
}


static bool
HasAtomHeader(const uint8* image, size_t imageSize,
	amdgpu_vbios_info* info)
{
	if (imageSize < 0x4a)
		return false;

	const uint16 atomOffset = ReadLE16(image + 0x48);
	if (atomOffset == 0 || !RangeIsValid(atomOffset, 8, imageSize))
		return false;

	const uint8* header = image + atomOffset;
	if (memcmp(header + 4, "ATOM", 4) != 0
		&& memcmp(header + 4, "MOTA", 4) != 0) {
		return false;
	}

	const uint16 headerSize = ReadLE16(header);
	if (headerSize < 34 || !RangeIsValid(atomOffset, headerSize, imageSize))
		return false;

	const uint16 masterDataOffset = ReadLE16(header + 32);
	if (masterDataOffset != 0) {
		if (!RangeIsValid(masterDataOffset, 4, imageSize))
			return false;
		const uint16 masterDataSize = ReadLE16(image + masterDataOffset);
		if (masterDataSize < 4
			|| !RangeIsValid(masterDataOffset, masterDataSize, imageSize)) {
			return false;
		}
	}

	info->atom_header_offset = atomOffset;
	info->master_data_table_offset = masterDataOffset;
	info->atom_header_size = headerSize;
	info->atom_format_revision = header[2];
	info->atom_content_revision = header[3];
	info->is_atom_bios = true;
	return true;
}


status_t
amdgpu_parse_vbios_image(const void* data, size_t dataSize,
	amdgpu_vbios_info* info)
{
	if (info != NULL)
		memset(info, 0, sizeof(*info));

	if (data == NULL || info == NULL || dataSize < 0x1a
		|| dataSize > 0xffffffffU) {
		return B_BAD_VALUE;
	}

	const uint8* bytes = (const uint8*)data;
	size_t imageOffset = 0;
	bool foundAtom = false;
	amdgpu_vbios_info atomInfo;
	memset(&atomInfo, 0, sizeof(atomInfo));

	/*
	 * PCI option ROM images are linked by their PCIR image-length and
	 * indicator fields. Bound the number of iterations by the minimum
	 * legal image size to make malformed chains deterministic.
	 */
	size_t maxImages = dataSize / 512 + 1;
	for (size_t imageIndex = 0; imageIndex < maxImages; imageIndex++) {
		if (!RangeIsValid(imageOffset, 0x1a, dataSize))
			return B_BAD_VALUE;

		const uint8* image = bytes + imageOffset;
		if (image[0] != 0x55 || image[1] != 0xaa)
			return B_BAD_VALUE;

		const uint16 pcirRelativeOffset = ReadLE16(image + 0x18);
		if (pcirRelativeOffset < 0x1a
			|| !RangeIsValid(pcirRelativeOffset, 0x16, dataSize - imageOffset))
			return B_BAD_VALUE;

		const uint8* pcir = image + pcirRelativeOffset;
		if (memcmp(pcir, "PCIR", 4) != 0)
			return B_BAD_VALUE;

		const uint16 pcirSize = ReadLE16(pcir + 10);
		if (pcirSize < 0x16
			|| !RangeIsValid(pcirRelativeOffset, pcirSize,
				dataSize - imageOffset)) {
			return B_BAD_VALUE;
		}

		const uint16 imageLengthUnits = ReadLE16(pcir + 16);
		if (imageLengthUnits == 0)
			return B_BAD_VALUE;

		const size_t imageSize = (size_t)imageLengthUnits * 512;
		if (!RangeIsValid(imageOffset, imageSize, dataSize)
			|| !RangeIsValid(pcirRelativeOffset, pcirSize, imageSize)) {
			return B_BAD_VALUE;
		}

		uint8 checksum = 0;
		for (size_t i = 0; i < imageSize; i++)
			checksum = (uint8)(checksum + image[i]);
		if (checksum != 0)
			return B_BAD_VALUE;

		const uint8 codeType = pcir[20];
		const uint8 indicator = pcir[21];
		if (!foundAtom) {
			amdgpu_vbios_info candidate;
			memset(&candidate, 0, sizeof(candidate));
			if (ReadLE16(pcir + 4) == 0x1002
				&& HasAtomHeader(image, imageSize, &candidate)) {
				candidate.vendor_id = ReadLE16(pcir + 4);
				candidate.device_id = ReadLE16(pcir + 6);
				candidate.image_offset = (uint32)imageOffset;
				candidate.image_size = (uint32)imageSize;
				candidate.pcir_offset = (uint32)(imageOffset
					+ pcirRelativeOffset);
				candidate.image_code_type = codeType;
				candidate.image_indicator = indicator;
				atomInfo = candidate;
				foundAtom = true;
			}
		}

		imageOffset += imageSize;
		if ((indicator & 0x80) != 0) {
			*info = atomInfo;
			return foundAtom ? B_OK : B_ENTRY_NOT_FOUND;
		}

		if (imageOffset >= dataSize)
			return B_BAD_VALUE;
	}

	return B_BAD_VALUE;
}
