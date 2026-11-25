/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_register_aperture.h"


status_t
amdgpu_resolve_register_aperture(const amdgpu_pci_device* device,
	const amdgpu_device_resources* resources, uint8 bar,
	uint64 barRelativeOffset, uint64 apertureSize,
	amdgpu_register_aperture* aperture)
{
	if (device == NULL || resources == NULL || aperture == NULL || bar >= 6
		|| apertureSize == 0)
		return B_BAD_VALUE;

	if (resources->magic != AMDGPU_DEVICE_RESOURCES_MAGIC
		|| !resources->reserved
		|| resources->mmio_area[bar] < 0
		|| resources->mmio[bar] == NULL
		|| resources->mmio_size[bar] == 0)
		return B_NOT_ALLOWED;

	if (resources->pci.vendor_id != device->pci.vendor_id
		|| resources->pci.device_id != device->pci.device_id
		|| resources->pci.bus != device->pci.bus
		|| resources->pci.device != device->pci.device
		|| resources->pci.function != device->pci.function)
		return B_BAD_VALUE;

	if ((device->mmio_bar_mask & (1u << bar)) == 0
		|| (device->prefetchable_bar_mask & (1u << bar)) != 0)
		return B_NOT_SUPPORTED;

	const uint64 mappedSize = resources->mmio_size[bar];
	if (resources->mmio_physical[bar] == 0
		|| device->mmio_bar_size[bar] == 0
		|| mappedSize > device->mmio_bar_size[bar])
		return B_BAD_DATA;
	if (barRelativeOffset > mappedSize
		|| apertureSize > mappedSize - barRelativeOffset)
		return B_BAD_VALUE;

	if (barRelativeOffset > (uint64)~(uint32)0
		|| apertureSize > (uint64)~(uint32)0)
		return B_NOT_SUPPORTED;

	aperture->bar = bar;
	aperture->offset = barRelativeOffset;
	aperture->size = apertureSize;
	aperture->address = (volatile uint8*)resources->mmio[bar]
		+ (size_t)barRelativeOffset;
	return B_OK;
}


status_t
amdgpu_register_offset(const amdgpu_register_aperture* aperture,
	uint64 registerOffset, uint32* mappedOffset)
{
	if (aperture == NULL || mappedOffset == NULL
		|| aperture->address == NULL || aperture->size < sizeof(uint32))
		return B_BAD_VALUE;

	if ((registerOffset & (sizeof(uint32) - 1)) != 0)
		return B_BAD_VALUE;
	if (registerOffset > aperture->size
		|| sizeof(uint32) > aperture->size - registerOffset)
		return B_BAD_VALUE;
	if (registerOffset > (uint64)~(uint32)0)
		return B_NOT_SUPPORTED;

	*mappedOffset = (uint32)registerOffset;
	return B_OK;
}
