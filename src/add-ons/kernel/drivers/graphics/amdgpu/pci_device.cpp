/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Conservative PCI discovery. Records endpoint identity and validated BAR
 * metadata only; it does not reserve devices or touch hardware.
 */
#include <string.h>
#include "pci_device.h"

static bool
IsAMDDisplay(const pci_info& info)
{
	return info.vendor_id == 0x1002 && info.class_base == PCI_display;
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
		if (!IsAMDDisplay(info))
			continue;

		found = true;
		if (*count >= capacity)
			continue;

		amdgpu_pci_device& device = devices[*count];
		memset(&device, 0, sizeof(device));
		device.pci = info;
		ReadMemoryBARMetadata(info, device);
		(*count)++;
	}
	return found ? B_OK : B_ENTRY_NOT_FOUND;
}
