/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_vfct.h"

#include <string.h>


status_t
amdgpu_vfct_device_from_pci_device(const amdgpu_pci_device* pciDevice,
	amdgpu_vfct_device* vfctDevice)
{
	if (vfctDevice != NULL)
		memset(vfctDevice, 0, sizeof(*vfctDevice));

	if (pciDevice == NULL || vfctDevice == NULL)
		return B_BAD_VALUE;

	const pci_info& pci = pciDevice->pci;
	if (pci.vendor_id != 0x1002 || pci.device_id == 0
		|| pci.device > 31 || pci.function > 7
		|| (pci.header_type & PCI_header_type_mask) != PCI_header_type_generic
		|| (pciDevice->identification_flags
			& (AMDGPU_PCI_ID_EXACT | AMDGPU_PCI_ID_IP_DISCOVERY)) == 0) {
		return B_BAD_VALUE;
	}

	vfctDevice->bus = pci.bus;
	vfctDevice->device = pci.device;
	vfctDevice->function = pci.function;
	vfctDevice->vendor_id = pci.vendor_id;
	vfctDevice->device_id = pci.device_id;
	return B_OK;
}

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


static bool
RangeIsValid(size_t offset, size_t length, size_t limit)
{
	return offset <= limit && length <= limit - offset;
}


static bool
HasValidACPIChecksum(const uint8* data, size_t size)
{
	uint8 checksum = 0;
	for (size_t i = 0; i < size; i++)
		checksum = (uint8)(checksum + data[i]);
	return checksum == 0;
}


status_t
amdgpu_parse_vfct_table(const void* data, size_t dataSize,
	const amdgpu_vfct_device* device, amdgpu_vfct_info* info)
{
	if (info != NULL)
		memset(info, 0, sizeof(*info));

	/* ACPI header (36) + VFCT-specific header (40). */
	static const size_t kVFCTHeaderSize = 76;
	static const size_t kVFCTImageHeaderSize = 28;

	if (data == NULL || device == NULL || info == NULL
		|| dataSize < kVFCTHeaderSize || dataSize > 0xffffffffU
		|| device->vendor_id == 0 || device->device_id == 0
		|| device->bus > 255 || device->device > 31 || device->function > 7) {
		return B_BAD_VALUE;
	}

	const uint8* bytes = (const uint8*)data;
	if (memcmp(bytes, "VFCT", 4) != 0)
		return B_BAD_VALUE;

	const uint32 tableLength = ReadLE32(bytes + 4);
	if (tableLength < kVFCTHeaderSize || tableLength > dataSize
		|| !RangeIsValid(0, tableLength, dataSize)) {
		return B_BAD_VALUE;
	}

	if (!HasValidACPIChecksum(bytes, tableLength))
		return B_BAD_VALUE;

	const uint32 vbiosImageOffset = ReadLE32(bytes + 0x34);
	const uint32 lib1ImageOffset = ReadLE32(bytes + 0x38);
	if (vbiosImageOffset < kVFCTHeaderSize
		|| vbiosImageOffset > tableLength) {
		return B_BAD_VALUE;
	}

	/*
	 * VFCT stores GOP VBIOS records in one region and optional LIB1 records
	 * in another. Do not interpret the LIB1 region as VBIOS entries.
	 */
	size_t entriesEnd = tableLength;
	if (lib1ImageOffset != 0) {
		if (lib1ImageOffset < vbiosImageOffset
			|| lib1ImageOffset > tableLength) {
			return B_BAD_VALUE;
		}
		entriesEnd = lib1ImageOffset;
	}

	size_t offset = vbiosImageOffset;
	while (offset < entriesEnd) {
		if (!RangeIsValid(offset, kVFCTImageHeaderSize, entriesEnd))
			return B_BAD_VALUE;

		const uint8* entry = bytes + offset;
		const uint32 bus = ReadLE32(entry);
		const uint32 pciDevice = ReadLE32(entry + 4);
		const uint32 function = ReadLE32(entry + 8);
		const uint16 vendorID = ReadLE16(entry + 12);
		const uint16 deviceID = ReadLE16(entry + 14);
		const uint16 subsystemVendorID = ReadLE16(entry + 16);
		const uint16 subsystemDeviceID = ReadLE16(entry + 18);
		const uint32 revision = ReadLE32(entry + 20);
		const uint32 imageLength = ReadLE32(entry + 24);

		if (bus > 255 || pciDevice > 31 || function > 7)
			return B_BAD_VALUE;

		const size_t imageOffset = offset + kVFCTImageHeaderSize;
		if (imageLength == 0
			|| !RangeIsValid(imageOffset, imageLength, entriesEnd)) {
			return B_BAD_VALUE;
		}

		const bool matches = vendorID == device->vendor_id
			&& deviceID == device->device_id
			&& pciDevice == device->device
			&& function == device->function;

		if (matches) {
			if (vendorID != 0x1002)
				return B_ENTRY_NOT_FOUND;

			amdgpu_vfct_info candidate;
			memset(&candidate, 0, sizeof(candidate));
			status_t status = amdgpu_parse_vbios_image(bytes + imageOffset,
				imageLength, &candidate.vbios);
			if (status != B_OK)
				return status == B_ENTRY_NOT_FOUND ? B_BAD_VALUE : status;

			if (candidate.vbios.vendor_id != vendorID
				|| candidate.vbios.device_id != deviceID) {
				return B_BAD_VALUE;
			}

			candidate.image_offset = (uint32)imageOffset;
			candidate.image_size = imageLength;
			candidate.entry_offset = (uint32)offset;
			candidate.bus = bus;
			candidate.device = pciDevice;
			candidate.function = function;
			candidate.vendor_id = vendorID;
			candidate.device_id = deviceID;
			candidate.subsystem_vendor_id = subsystemVendorID;
			candidate.subsystem_device_id = subsystemDeviceID;
			candidate.revision = revision;
			candidate.bus_matches = bus == device->bus;
			*info = candidate;
			return B_OK;
		}

		offset = imageOffset + imageLength;
	}

	return offset == entriesEnd ? B_ENTRY_NOT_FOUND : B_BAD_VALUE;
}
