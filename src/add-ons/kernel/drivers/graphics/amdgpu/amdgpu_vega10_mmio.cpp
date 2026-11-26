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
	 * The first 0x80000 bytes contain the documented SOC15 register hole.
	 * Do not infer that the mapping of that hole or later addresses is
	 * linear. Only the lower, directly mapped register range is enabled.
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

	/* The 4 KiB remap hole and everything beyond it are not linear. */
	if (registerByteOffset >= AMDGPU_VEGA10_MMIO_HOLE_OFFSET
		|| sizeof(uint32) > AMDGPU_VEGA10_MMIO_HOLE_OFFSET
			- registerByteOffset) {
		return B_NOT_SUPPORTED;
	}

	return amdgpu_register_offset(aperture, registerByteOffset,
		mappedOffset);
}
