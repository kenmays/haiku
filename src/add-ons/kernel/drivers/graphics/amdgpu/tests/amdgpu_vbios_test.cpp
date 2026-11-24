/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_vbios.h"

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
FinishChecksum(std::vector<uint8>& image)
{
	uint8 sum = 0;
	for (size_t i = 0; i < image.size(); i++)
		sum = (uint8)(sum + image[i]);
	image.back() = (uint8)(image.back() - sum);
}


static std::vector<uint8>
MakeImage(bool atom, uint8 indicator, uint8 codeType = 0)
{
	std::vector<uint8> image(512, 0);
	image[0] = 0x55;
	image[1] = 0xaa;
	WriteLE16(&image[0x18], 0x1c);

	uint8* pcir = &image[0x1c];
	memcpy(pcir, "PCIR", 4);
	WriteLE16(pcir + 4, 0x1002);
	WriteLE16(pcir + 6, 0x73bf);
	WriteLE16(pcir + 10, 0x18);
	WriteLE16(pcir + 16, 1);
	pcir[20] = codeType;
	pcir[21] = indicator;

	if (atom) {
		WriteLE16(&image[0x48], 0x80);
		WriteLE16(&image[0x80], 40);
		image[0x82] = 2;
		image[0x83] = 2;
		memcpy(&image[0x84], "ATOM", 4);
		WriteLE16(&image[0x80 + 32], 0x100);
		WriteLE16(&image[0x100], 4);
	}

	FinishChecksum(image);
	return image;
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
	amdgpu_vbios_info info;

	std::vector<uint8> first = MakeImage(false, 0, 3);
	std::vector<uint8> second = MakeImage(true, 0x80);
	std::vector<uint8> chain(first);
	chain.insert(chain.end(), second.begin(), second.end());

	success &= Check(amdgpu_parse_vbios_image(&chain[0], chain.size(),
		&info) == B_OK, "valid multi-image PCI ROM chain");
	success &= Check(info.image_offset == 512 && info.image_size == 512
		&& info.pcir_offset == 512 + 0x1c, "selected ATOM image metadata");
	success &= Check(info.atom_header_offset == 0x80
		&& info.master_data_table_offset == 0x100
		&& info.atom_format_revision == 2
		&& info.atom_content_revision == 2, "ATOM header decoded");

	std::vector<uint8> noAtom = MakeImage(false, 0x80, 3);
	success &= Check(amdgpu_parse_vbios_image(&noAtom[0], noAtom.size(),
		&info) == B_ENTRY_NOT_FOUND, "valid non-ATOM ROM chain");

	std::vector<uint8> badChecksum = second;
	badChecksum[0x200 - 1] ^= 1;
	success &= Check(amdgpu_parse_vbios_image(&badChecksum[0],
		badChecksum.size(), &info) == B_BAD_VALUE,
		"ROM image checksum corruption rejected");

	std::vector<uint8> badPcir = second;
	memcpy(&badPcir[0x1c], "FAIL", 4);
	FinishChecksum(badPcir);
	success &= Check(amdgpu_parse_vbios_image(&badPcir[0],
		badPcir.size(), &info) == B_BAD_VALUE, "invalid PCIR signature rejected");

	std::vector<uint8> overlappingPcir = second;
	WriteLE16(&overlappingPcir[0x18], 0x10);
	FinishChecksum(overlappingPcir);
	success &= Check(amdgpu_parse_vbios_image(&overlappingPcir[0],
		overlappingPcir.size(), &info) == B_BAD_VALUE,
		"PCIR pointer into fixed ROM header rejected");

	std::vector<uint8> badLength = second;
	WriteLE16(&badLength[0x1c + 16], 2);
	FinishChecksum(badLength);
	success &= Check(amdgpu_parse_vbios_image(&badLength[0],
		badLength.size(), &info) == B_BAD_VALUE,
		"ROM image length outside source rejected");

	std::vector<uint8> missingLast = MakeImage(false, 0);
	success &= Check(amdgpu_parse_vbios_image(&missingLast[0],
		missingLast.size(), &info) == B_BAD_VALUE,
		"unterminated ROM chain rejected");

	std::vector<uint8> badAtom = second;
	WriteLE16(&badAtom[0x48], 0x1f0);
	FinishChecksum(badAtom);
	success &= Check(amdgpu_parse_vbios_image(&badAtom[0],
		badAtom.size(), &info) == B_ENTRY_NOT_FOUND,
		"out-of-bounds ATOM header ignored safely");

	success &= Check(amdgpu_parse_vbios_image(&chain[0], 0, &info)
		== B_BAD_VALUE, "empty input rejected");
	success &= Check(amdgpu_parse_vbios_image(&chain[0], chain.size(), NULL)
		== B_BAD_VALUE, "null result rejected");

	if (!success)
		return 1;

	printf("AMDGPU VBIOS parser tests passed.\n");
	return 0;
}
