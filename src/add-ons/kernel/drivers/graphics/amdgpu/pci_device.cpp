/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Conservative PCI discovery. Records endpoint identity and validated BAR
 * metadata only; it does not reserve devices or touch hardware.
 */
#include <string.h>
#include "pci_device.h"
#include "pci_ids.h"

static uint8
IdentifyASIC(uint16 deviceID, uint8 revision, uint16* family)
{
	(void)revision;
	*family = AMDGPU_ASIC_UNKNOWN;

	// Deliberately curated IDs, cross-checked against Linux amdgpu_drv.c.
	// This table is an identification aid, not an enablement whitelist.
	switch (deviceID) {
		case 0x6780: case 0x6784: case 0x6788: case 0x678a:
		case 0x6790: case 0x6791: case 0x6792: case 0x6798:
		case 0x6799: case 0x679a: case 0x679b: case 0x679e:
		case 0x679f:
			*family = AMDGPU_ASIC_TAHITI;
			return AMDGPU_GENERATION_GCN;
		case 0x6640: case 0x6641: case 0x6649: case 0x6658:
		case 0x665c: case 0x665d:
			*family = AMDGPU_ASIC_BONAIRE;
			return AMDGPU_GENERATION_GCN;
		case 0x67a0: case 0x67a1: case 0x67a2: case 0x67a8:
		case 0x67a9: case 0x67aa: case 0x67b0: case 0x67b1:
			*family = AMDGPU_ASIC_HAWAII;
			return AMDGPU_GENERATION_GCN;
		case 0x67df: case 0x67ef: case 0x67ff:
			*family = AMDGPU_ASIC_POLARIS10;
			return AMDGPU_GENERATION_GCN;
		case 0x6860: case 0x6861: case 0x6862: case 0x6863:
		case 0x6864: case 0x6867: case 0x6868: case 0x6869:
		case 0x686a: case 0x686b: case 0x686c: case 0x686d:
		case 0x686e: case 0x686f: case 0x687f:
			*family = AMDGPU_ASIC_VEGA10;
			return AMDGPU_GENERATION_GCN;
		case 0x66a0: case 0x66a1: case 0x66a2: case 0x66a3:
		case 0x66a4:
			*family = AMDGPU_ASIC_VEGA20;
			return AMDGPU_GENERATION_GCN;
		case 0x7310: case 0x7312: case 0x7318: case 0x7319:
		case 0x731a: case 0x731b: case 0x731e: case 0x731f:
			*family = AMDGPU_ASIC_NAVI10;
			return AMDGPU_GENERATION_RDNA1;
		case 0x73a0: case 0x73a1: case 0x73a2: case 0x73a3:
		case 0x73ab: case 0x73ae: case 0x73bf:
			*family = AMDGPU_ASIC_NAVI21;
			return AMDGPU_GENERATION_RDNA2;
		case 0x744c: case 0x7448: case 0x744a: case 0x744b:
			*family = AMDGPU_ASIC_NAVI31;
			return AMDGPU_GENERATION_RDNA3;
		default:
			return AMDGPU_GENERATION_UNKNOWN;
	}
}


static bool
IsExactAMDGPUDeviceID(uint16 deviceID)
{
	uint32 low = 0;
	uint32 high = kAMDGPUExactDeviceIDCount;
	while (low < high) {
		uint32 middle = low + (high - low) / 2;
		if (kAMDGPUExactDeviceIDs[middle] == deviceID)
			return true;
		if (kAMDGPUExactDeviceIDs[middle] < deviceID)
			low = middle + 1;
		else
			high = middle;
	}
	return false;
}


static bool
IsAMDGPUFunction(const pci_info& info)
{
	if (info.vendor_id != 0x1002)
		return false;

	// Linux amdgpu's table includes exact device IDs plus class-wide
	// IP-discovery matches for VGA, other display, and processing accelerators.
	if (info.class_base == PCI_display)
		return true;
	return info.class_base == PCI_processing_accelerator
		&& info.class_sub == 0x00;
}

static void
ReadMemoryBARMetadata(const pci_info& info, amdgpu_pci_device& device)
{
	if ((info.header_type & 0x7f) != 0)
		return;

	for (uint32 bar = 0; bar < 6; bar++) {
		const uint8 flags = info.u.h0.base_register_flags[bar];
		if ((flags & PCI_address_space) != 0)
			continue;

		const uint32 size = info.u.h0.base_register_sizes[bar];
		if (size == 0)
			continue;

		const uint8 type = flags & PCI_address_type;
		if (type != PCI_address_type_32
			&& type != PCI_address_type_32_low
			&& type != PCI_address_type_64)
			continue;

		device.mmio_bar_mask |= (uint8)(1u << bar);
		if ((flags & PCI_address_prefetchable) != 0)
			device.prefetchable_bar_mask |= (uint8)(1u << bar);
		device.mmio_bar_size[bar] = size;

		// A 64-bit BAR consumes the next slot; it is not an independent BAR.
		if (type == PCI_address_type_64 && bar < 5)
			bar++;
	}
}

status_t
amdgpu_enumerate(pci_module_info* pci, amdgpu_pci_device* devices,
	uint32 capacity, uint32* count)
{
	if (pci == NULL || pci->get_nth_pci_info == NULL || count == NULL)
		return B_BAD_VALUE;
	*count = 0;
	if (capacity != 0 && devices == NULL)
		return B_BAD_VALUE;

	bool found = false;
	for (long index = 0; index < AMDGPU_MAX_PCI_FUNCTIONS; index++) {
		pci_info info;
		status_t status = (status_t)pci->get_nth_pci_info(index, &info);
		if (status != B_OK)
			break;
		if (!IsAMDGPUFunction(info))
			continue;

		found = true;
		if (*count >= capacity)
			continue;

		amdgpu_pci_device& device = devices[*count];
		memset(&device, 0, sizeof(device));
		device.pci = info;
		if (IsExactAMDGPUDeviceID(info.device_id))
			device.identification_flags |= AMDGPU_PCI_ID_EXACT;
		if (info.class_base == PCI_display
			|| (info.class_base == PCI_processing_accelerator
				&& info.class_sub == 0x00)) {
			device.identification_flags |= AMDGPU_PCI_ID_IP_DISCOVERY;
		}
		device.generation = IdentifyASIC(info.device_id, info.revision,
			&device.asic_family);
		ReadMemoryBARMetadata(info, device);
		(*count)++;
	}
	return found ? B_OK : B_ENTRY_NOT_FOUND;
}
