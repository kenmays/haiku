/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "amdgpu_vega10_mmio.h"


status_t
amdgpu_vega10_resolve_register_aperture(
	const amdgpu_pci_device* device,
	const amdgpu_device_resources* resources,
	const amdgpu_ip_discovery_info* discovery,
	amdgpu_register_aperture* aperture)
{
	if (device == NULL || resources == NULL || discovery == NULL
		|| aperture == NULL) {
		return B_BAD_VALUE;
	}

	if (device->pci.vendor_id != 0x1002
		|| device->asic_family != AMDGPU_ASIC_VEGA10
		|| (device->identification_flags & AMDGPU_PCI_ID_EXACT) == 0) {
		return B_NOT_SUPPORTED;
	}

	amdgpu_vega10_profile profile;
	status_t status = amdgpu_validate_vega10_profile(device, discovery,
		&profile);
	if (status != B_OK)
		return status;

	if (resources->mmio_size[AMDGPU_VEGA10_REGISTER_BAR]
		< AMDGPU_VEGA10_MMIO_MIN_SIZE) {
		return B_NOT_SUPPORTED;
	}

	status = amdgpu_resolve_register_aperture(device, resources,
		AMDGPU_VEGA10_REGISTER_BAR, 0,
		(uint64)resources->mmio_size[AMDGPU_VEGA10_REGISTER_BAR],
		aperture);
	if (status != B_OK)
		return status;

	/*
	 * Keep the kernel register mapping linear. The SOC15 rmmio_remap
	 * metadata describes a separate KFD userspace mapping page; it does
	 * not create a hole in the BAR window used by direct register access.
	 */
	if (aperture->bar != AMDGPU_VEGA10_REGISTER_BAR
		|| aperture->offset != 0
		|| aperture->size < AMDGPU_VEGA10_MMIO_MIN_SIZE
		|| aperture->address == NULL) {
		return B_BAD_DATA;
	}

	return B_OK;
}


status_t
amdgpu_vega10_register_mapped_offset(
	const amdgpu_register_aperture* aperture, uint64 registerByteOffset,
	uint32* mappedOffset)
{
	if (aperture == NULL || mappedOffset == NULL
		|| aperture->address == NULL) {
		return B_BAD_VALUE;
	}

	if (aperture->bar != AMDGPU_VEGA10_REGISTER_BAR
		|| aperture->offset != 0
		|| aperture->size < AMDGPU_VEGA10_MMIO_MIN_SIZE) {
		return B_BAD_DATA;
	}

	/*
	 * Direct kernel MMIO access uses rmmio + byte offset. KFD's remap
	 * page at 0x7f000 is an additional userspace mapping, not an aperture
	 * hole, so normal bounds/alignment checks are sufficient here.
	 */
	return amdgpu_register_offset(aperture, registerByteOffset,
		mappedOffset);
}
