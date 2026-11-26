/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <ByteOrder.h>

#include "amdgpu_vega10_probe.h"
#include "amdgpu_vega10_registers.h"
#include "amdgpu_vega10_reg_access.h"


status_t
amdgpu_vega10_read_gb_addr_config(
	const amdgpu_pci_device* device,
	const amdgpu_device_resources* resources,
	const amdgpu_ip_discovery_info* discovery, uint32* value)
{
	if (device == NULL || resources == NULL || discovery == NULL
		|| value == NULL) {
		return B_BAD_VALUE;
	}

	amdgpu_register_aperture aperture;
	status_t status = amdgpu_vega10_resolve_register_aperture(device,
		resources, discovery, &aperture);
	if (status != B_OK)
		return status;

	/*
	 * gc_9_0_offset.h defines mmGB_ADDR_CONFIG as register index 0x063e,
	 * BASE_IDX 0. Vega 10 GC segment zero begins at dword index 0x2000.
	 * SOC15 offsets are dword indices, so the byte offset is 0x98f8.
	 */
	status = amdgpu_vega10_check_register_access(11, 0, 0,
		AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX,
		AMDGPU_REGISTER_ACCESS_READ);
	if (status != B_OK)
		return status;

	uint32 registerByteOffset;
	status = amdgpu_vega10_register_byte_offset(11, 0, 0,
		AMDGPU_VEGA10_GB_ADDR_CONFIG_REGISTER_INDEX, &registerByteOffset);
	if (status != B_OK)
		return status;
	if (registerByteOffset != AMDGPU_VEGA10_GB_ADDR_CONFIG_BYTE_OFFSET)
		return B_BAD_DATA;

	uint32 mappedOffset;
	status = amdgpu_vega10_register_mapped_offset(&aperture,
		registerByteOffset, &mappedOffset);
	if (status != B_OK)
		return status;

	if ((mappedOffset & (sizeof(uint32) - 1)) != 0)
		return B_BAD_VALUE;

	volatile const uint32* registerAddress =
		(volatile const uint32*)(aperture.address + mappedOffset);

	/*
	 * The aperture is mapped uncached. Keep the MMIO load explicit and
	 * ordered; convert the PCI little-endian register value for the host.
	 */
	__sync_synchronize();
	uint32 rawValue = *registerAddress;
	__sync_synchronize();

	*value = B_LENDIAN_TO_HOST_INT32(rawValue);
	return B_OK;
}
