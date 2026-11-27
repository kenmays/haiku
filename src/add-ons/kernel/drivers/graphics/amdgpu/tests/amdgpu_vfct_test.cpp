/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_vfct.h"
#include "../amdgpu_vfct_provider.h"
#include "../pci_device.h"

#include <ACPI.h>

#include <stdio.h>
#include <string.h>
#include <vector>

static void
WriteLE16(uint8* data, uint16 value)
{
	data[0] = value & 0xff;
	data[1] = value >> 8;
}


static void
WriteLE32(uint8* data, uint32 value)
{
	data[0] = value & 0xff;
	data[1] = (value >> 8) & 0xff;
	data[2] = (value >> 16) & 0xff;
	data[3] = (value >> 24) & 0xff;
}


static void
FinishROMChecksum(std::vector<uint8>& image)
{
	uint8 sum = 0;
	for (size_t i = 0; i < image.size(); i++)
		sum = (uint8)(sum + image[i]);
	image.back() = (uint8)(image.back() - sum);
}


static std::vector<uint8>
MakeROM(uint16 vendorID, uint16 deviceID)
{
	std::vector<uint8> image(512, 0);
	image[0] = 0x55;
	image[1] = 0xaa;
	WriteLE16(&image[0x18], 0x1c);
	memcpy(&image[0x1c], "PCIR", 4);
	WriteLE16(&image[0x1c + 4], vendorID);
	WriteLE16(&image[0x1c + 6], deviceID);
	WriteLE16(&image[0x1c + 10], 0x18);
	WriteLE16(&image[0x1c + 16], 1);
	image[0x1c + 21] = 0x80;

	WriteLE16(&image[0x48], 0x80);
	WriteLE16(&image[0x80], 40);
	image[0x82] = 2;
	image[0x83] = 2;
	memcpy(&image[0x84], "ATOM", 4);
	WriteLE16(&image[0x80 + 32], 0x100);
	WriteLE16(&image[0x100], 4);
	FinishROMChecksum(image);
	return image;
}


static void
FinishACPIChecksum(std::vector<uint8>& table)
{
	table[9] = 0;
	uint8 sum = 0;
	for (size_t i = 0; i < table.size(); i++)
		sum = (uint8)(sum + table[i]);
	table[9] = (uint8)(0 - sum);
}


static void
AppendEntry(std::vector<uint8>& table, uint32 bus, uint32 pciDevice,
	uint32 function, uint16 vendorID, uint16 deviceID,
	const std::vector<uint8>& image)
{
	const size_t offset = table.size();
	table.resize(offset + 28 + image.size(), 0);
	uint8* entry = &table[offset];
	WriteLE32(entry, bus);
	WriteLE32(entry + 4, pciDevice);
	WriteLE32(entry + 8, function);
	WriteLE16(entry + 12, vendorID);
	WriteLE16(entry + 14, deviceID);
	WriteLE16(entry + 16, 0x1022);
	WriteLE16(entry + 18, 0x0b00);
	WriteLE32(entry + 20, 7);
	WriteLE32(entry + 24, image.size());
	memcpy(entry + 28, &image[0], image.size());
}


static std::vector<uint8>
MakeTable()
{
	std::vector<uint8> table(76, 0);
	memcpy(&table[0], "VFCT", 4);
	WriteLE32(&table[4], 0);
	table[8] = 1;
	memcpy(&table[10], "HAIKU ", 6);
	memcpy(&table[16], "AMDGPUVF", 8);
	WriteLE32(&table[0x34], 76);
	WriteLE32(&table[0x38], 0);

	std::vector<uint8> other = MakeROM(0x1002, 0x731f);
	std::vector<uint8> target = MakeROM(0x1002, 0x73bf);
	AppendEntry(table, 1, 2, 0, 0x1002, 0x731f, other);
	AppendEntry(table, 2, 3, 1, 0x1002, 0x73bf, target);
	WriteLE32(&table[4], table.size());
	FinishACPIChecksum(table);
	return table;
}


static std::vector<uint8> sProviderTable;
static int sGetTableCalls = 0;
static int sPutTableCalls = 0;
static bool sFailGetTable = false;
static bool sReturnNullTable = false;
static bool sFailPutTable = false;


static status_t
FakeGetTable(const char* signature, uint32 instance, void** tableHeader)
{
	sGetTableCalls++;
	if (signature == NULL || strcmp(signature, "VFCT") != 0
		|| instance != 1 || tableHeader == NULL || sFailGetTable) {
		return B_ENTRY_NOT_FOUND;
	}
	*tableHeader = sReturnNullTable ? NULL : &sProviderTable[0];
	return B_OK;
}


static status_t
FakePutTable(void* tableHeader)
{
	sPutTableCalls++;
	if (tableHeader == NULL || sProviderTable.empty()
		|| tableHeader != &sProviderTable[0]) {
		return B_BAD_VALUE;
	}
	return sFailPutTable ? B_IO_ERROR : B_OK;
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
	std::vector<uint8> table = MakeTable();
	amdgpu_vfct_device device = {2, 3, 1, 0x1002, 0x73bf};
	amdgpu_vfct_info info;

	success &= Check(amdgpu_parse_vfct_table(&table[0], table.size(),
		&device, &info) == B_OK, "select matching VBIOS entry");
	success &= Check(info.vendor_id == 0x1002 && info.device_id == 0x73bf
		&& info.subsystem_vendor_id == 0x1022
		&& info.subsystem_device_id == 0x0b00,
		"VFCT PCI and subsystem identity decoded");
	success &= Check(info.image_offset == info.entry_offset + 28
		&& info.image_size == 512 && info.vbios.is_atom_bios,
		"embedded ATOMBIOS image validated");
	success &= Check(info.bus == 2 && info.device == 3
		&& info.function == 1 && info.bus_matches,
		"exact PCI BDF match reported");

	amdgpu_vfct_device renumbered = {9, 3, 1, 0x1002, 0x73bf};
	success &= Check(amdgpu_parse_vfct_table(&table[0], table.size(),
		&renumbered, &info) == B_OK && !info.bus_matches,
		"POST bus mismatch accepted for matching device/function");

	/* Translate the retained PCI candidate without claiming the function. */
	amdgpu_pci_device pciDevice;
	memset(&pciDevice, 0, sizeof(pciDevice));
	pciDevice.pci.vendor_id = 0x1002;
	pciDevice.pci.device_id = 0x73bf;
	pciDevice.pci.bus = 2;
	pciDevice.pci.device = 3;
	pciDevice.pci.function = 1;
	pciDevice.pci.header_type = PCI_header_type_generic;
	pciDevice.identification_flags = AMDGPU_PCI_ID_EXACT;
	amdgpu_vfct_device translated;
	success &= Check(amdgpu_vfct_device_from_pci_device(&pciDevice,
		&translated) == B_OK && translated.bus == 2
		&& translated.device == 3 && translated.function == 1
		&& translated.vendor_id == 0x1002 && translated.device_id == 0x73bf,
		"enumerated PCI candidate maps to exact VFCT identity");
	pciDevice.pci.vendor_id = 0x1234;
	success &= Check(amdgpu_vfct_device_from_pci_device(&pciDevice,
		&translated) == B_BAD_VALUE, "non-AMD PCI candidate rejected");
	pciDevice.pci.vendor_id = 0x1002;
	pciDevice.identification_flags = 0;
	success &= Check(amdgpu_vfct_device_from_pci_device(&pciDevice,
		&translated) == B_BAD_VALUE, "unrecognized PCI candidate rejected");
	pciDevice.identification_flags = AMDGPU_PCI_ID_EXACT;
	pciDevice.pci.header_type = PCI_header_type_PCI_to_PCI_bridge;
	success &= Check(amdgpu_vfct_device_from_pci_device(&pciDevice,
		&translated) == B_BAD_VALUE, "PCI bridge function rejected");

	/*
	 * Exercise the provider's ownership contract: the ACPI table reference
	 * must be released after success, capacity failure, and parse failure.
	 */
	sProviderTable = MakeTable();
	acpi_table_module_info fakeACPI;
	memset(&fakeACPI, 0, sizeof(fakeACPI));
	fakeACPI.get_table = FakeGetTable;
	fakeACPI.put_table = FakePutTable;
	std::vector<uint8> copiedBIOS(512, 0);
	size_t copiedBIOSSize = 0;
	int putsBefore = sPutTableCalls;
	success &= Check(amdgpu_get_vbios_from_vfct(&fakeACPI, &device,
		&copiedBIOS[0], copiedBIOS.size(), &copiedBIOSSize, &info) == B_OK,
		"ACPI VFCT provider copies validated BIOS");
	success &= Check(copiedBIOSSize == 512
		&& memcmp(&copiedBIOS[0], &sProviderTable[76 + 28 + 512 + 28],
			512) == 0, "provider copies the selected image before release");
	success &= Check(sPutTableCalls == putsBefore + 1,
		"provider releases ACPI table after success");

	uint8 smallBIOS[16];
	putsBefore = sPutTableCalls;
	success &= Check(amdgpu_get_vbios_from_vfct(&fakeACPI, &device,
		smallBIOS, sizeof(smallBIOS), &copiedBIOSSize, &info)
		== B_BUFFER_OVERFLOW && copiedBIOSSize == 512,
		"provider reports required capacity");
	success &= Check(sPutTableCalls == putsBefore + 1,
		"provider releases ACPI table after capacity failure");

	putsBefore = sPutTableCalls;
	success &= Check(amdgpu_query_vbios_size_from_vfct(&fakeACPI, &device,
		&copiedBIOSSize, &info) == B_BUFFER_OVERFLOW
		&& copiedBIOSSize == 512 && info.image_size == 512,
		"provider supports a no-copy image-size query");
	success &= Check(sPutTableCalls == putsBefore + 1,
		"provider releases ACPI table after size query");

	sProviderTable[45] ^= 1;
	putsBefore = sPutTableCalls;
	success &= Check(amdgpu_get_vbios_from_vfct(&fakeACPI, &device,
		&copiedBIOS[0], copiedBIOS.size(), &copiedBIOSSize, &info)
		== B_BAD_VALUE, "provider rejects corrupt ACPI table");
	success &= Check(sPutTableCalls == putsBefore + 1,
		"provider releases ACPI table after parse failure");
	sProviderTable[45] ^= 1;

	sFailGetTable = true;
	putsBefore = sPutTableCalls;
	success &= Check(amdgpu_get_vbios_from_vfct(&fakeACPI, &device,
		&copiedBIOS[0], copiedBIOS.size(), &copiedBIOSSize, &info)
		== B_ENTRY_NOT_FOUND && sPutTableCalls == putsBefore,
		"provider does not release a table it failed to acquire");
	sFailGetTable = false;

	sReturnNullTable = true;
	putsBefore = sPutTableCalls;
	success &= Check(amdgpu_get_vbios_from_vfct(&fakeACPI, &device,
		&copiedBIOS[0], copiedBIOS.size(), &copiedBIOSSize, &info)
		== B_BAD_VALUE && sPutTableCalls == putsBefore + 1,
		"provider pairs successful null-table result with release attempt");
	sReturnNullTable = false;

	/* A failed release invalidates both copied bytes and returned metadata. */
	memset(&copiedBIOS[0], 0x5a, copiedBIOS.size());
	memset(&info, 0x5a, sizeof(info));
	copiedBIOSSize = 123;
	sFailPutTable = true;
	success &= Check(amdgpu_get_vbios_from_vfct(&fakeACPI, &device,
		&copiedBIOS[0], copiedBIOS.size(), &copiedBIOSSize, &info)
		== B_IO_ERROR, "provider propagates ACPI table release failure");
	bool bufferCleared = true;
	for (size_t i = 0; i < copiedBIOS.size(); i++)
		bufferCleared &= copiedBIOS[i] == 0;
	success &= Check(bufferCleared && copiedBIOSSize == 0
		&& info.image_size == 0 && info.vendor_id == 0,
		"failed ACPI release clears image and returned metadata");
	sFailPutTable = false;

	fakeACPI.put_table = NULL;
	int getsBefore = sGetTableCalls;
	success &= Check(amdgpu_get_vbios_from_vfct(&fakeACPI, &device,
		&copiedBIOS[0], copiedBIOS.size(), &copiedBIOSSize, &info)
		== B_NOT_SUPPORTED && sGetTableCalls == getsBefore,
		"provider refuses unpaired ACPI table access");

	amdgpu_vfct_device absent = {2, 4, 1, 0x1002, 0x73bf};
	success &= Check(amdgpu_parse_vfct_table(&table[0], table.size(),
		&absent, &info) == B_ENTRY_NOT_FOUND,
		"unmatched PCI function not selected");

	std::vector<uint8> badChecksum = table;
	badChecksum[40] ^= 1;
	success &= Check(amdgpu_parse_vfct_table(&badChecksum[0],
		badChecksum.size(), &device, &info) == B_BAD_VALUE,
		"ACPI table checksum corruption rejected");

	std::vector<uint8> badOffset = table;
	WriteLE32(&badOffset[0x34], 0x30);
	FinishACPIChecksum(badOffset);
	success &= Check(amdgpu_parse_vfct_table(&badOffset[0],
		badOffset.size(), &device, &info) == B_BAD_VALUE,
		"VBIOS image offset into VFCT header rejected");

	std::vector<uint8> badLength = table;
	WriteLE32(&badLength[76 + 28 - 4], 0xffffffff);
	FinishACPIChecksum(badLength);
	success &= Check(amdgpu_parse_vfct_table(&badLength[0],
		badLength.size(), &device, &info) == B_BAD_VALUE,
		"embedded image extent overflow rejected");

	std::vector<uint8> truncated = table;
	truncated.resize(truncated.size() - 100);
	WriteLE32(&truncated[4], truncated.size());
	FinishACPIChecksum(truncated);
	success &= Check(amdgpu_parse_vfct_table(&truncated[0],
		truncated.size(), &device, &info) == B_BAD_VALUE,
		"truncated matching image rejected");

	std::vector<uint8> badBDF = table;
	WriteLE32(&badBDF[76 + 4], 32);
	FinishACPIChecksum(badBDF);
	success &= Check(amdgpu_parse_vfct_table(&badBDF[0],
		badBDF.size(), &device, &info) == B_BAD_VALUE,
		"invalid PCI device number in VFCT rejected");

	std::vector<uint8> wrongROM = table;
	/* Change the embedded ROM PCIR device ID without changing VFCT identity. */
	const size_t secondROMOffset = 76 + 28 + 512 + 28;
	WriteLE16(&wrongROM[secondROMOffset + 0x1c + 6], 0x731f);
	/* Recompute the ROM checksum in-place. */
	{
		uint8 sum = 0;
		uint8* rom = &wrongROM[secondROMOffset];
		for (size_t i = 0; i < 512; i++)
			sum = (uint8)(sum + rom[i]);
		rom[511] = (uint8)(rom[511] - sum);
	}
	FinishACPIChecksum(wrongROM);
	success &= Check(amdgpu_parse_vfct_table(&wrongROM[0],
		wrongROM.size(), &device, &info) == B_BAD_VALUE,
		"VFCT identity cannot mask embedded ROM PCI-ID mismatch");

	success &= Check(amdgpu_parse_vfct_table(&table[0], table.size(),
		NULL, &info) == B_BAD_VALUE, "null device rejected");

	if (!success)
		return 1;
	printf("AMDGPU VFCT parser tests passed\n");
	return 0;
}
