/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * PCI ownership and MMIO lifetime helpers for the native Haiku AMDGPU driver.
 * These helpers are deliberately not called by the discovery-only driver
 * until a real device node and complete initialization/uninitialization path
 * are available.
 */
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <SupportDefs.h>
#include <string.h>

#include "device_resources.h"

static const char* const kAMDGPUDriverName = "amdgpu";


static void
InitializeResources(amdgpu_device_resources& resources)
{
	memset(&resources, 0, sizeof(resources));
	for (uint32 i = 0; i < 6; i++)
		resources.mmio_area[i] = -1;
	resources.magic = AMDGPU_DEVICE_RESOURCES_MAGIC;
}


status_t
amdgpu_init_device_resources(amdgpu_device_resources* resources)
{
	if (resources == NULL)
		return B_BAD_VALUE;
	if (resources->magic == AMDGPU_DEVICE_RESOURCES_MAGIC) {
		if (resources->reserved)
			return B_BUSY;
		for (uint32 i = 0; i < 6; i++) {
			if (resources->mmio_area[i] >= 0)
				return B_BUSY;
		}
	}
	InitializeResources(*resources);
	return B_OK;
}


status_t
amdgpu_claim_device(pci_module_info* pci, const amdgpu_pci_device* device,
	amdgpu_device_resources* resources)
{
	if (pci == NULL || device == NULL || resources == NULL
		|| pci->reserve_device == NULL || pci->unreserve_device == NULL) {
		return B_BAD_VALUE;
	}
	// The caller must zero-initialize storage and call the initializer first.
	if (resources->magic != AMDGPU_DEVICE_RESOURCES_MAGIC)
		return B_BAD_VALUE;
	if (device->pci.vendor_id != 0x1002
		|| (device->identification_flags
			& (AMDGPU_PCI_ID_EXACT | AMDGPU_PCI_ID_IP_DISCOVERY)) == 0) {
		return B_BAD_VALUE;
	}
	if (resources->reserved)
		return B_BUSY;
	for (uint32 i = 0; i < 6; i++) {
		if (resources->mmio_area[i] >= 0)
			return B_BUSY;
	}
	resources->pci = device->pci;

	status_t status = pci->reserve_device(device->pci.bus,
		device->pci.device, device->pci.function, kAMDGPUDriverName,
		resources);
	if (status != B_OK) {
		InitializeResources(*resources);
		return status;
	}

	resources->reserved = true;
	return B_OK;
}


status_t
amdgpu_map_mmio_bar(const amdgpu_pci_device* device,
	amdgpu_device_resources* resources, uint8 bar)
{
	if (device == NULL || resources == NULL || bar >= 6
		|| resources->magic != AMDGPU_DEVICE_RESOURCES_MAGIC
		|| !resources->reserved) {
		return B_BAD_VALUE;
	}
	if (resources->pci.vendor_id != device->pci.vendor_id
		|| resources->pci.device_id != device->pci.device_id
		|| resources->pci.bus != device->pci.bus
		|| resources->pci.device != device->pci.device
		|| resources->pci.function != device->pci.function) {
		return B_BAD_VALUE;
	}
	if (resources->mmio_area[bar] >= 0)
		return B_BUSY;
	if ((device->mmio_bar_mask & (1u << bar)) == 0)
		return B_BAD_VALUE;
	/*
	 * Prefetchable framebuffer/VRAM BARs are not register mappings.
	 * Vega 10's BAR 5 is the documented register aperture; permit its
	 * prefetchable attribute only for an exact ASIC match. It is still
	 * mapped with B_UNCACHED_MEMORY below, never write-combined.
	 */
	bool vega10RegisterBar = bar == 5
		&& device->pci.vendor_id == 0x1002
		&& device->asic_family == AMDGPU_ASIC_VEGA10
		&& (device->identification_flags & AMDGPU_PCI_ID_EXACT) != 0;
	if ((device->prefetchable_bar_mask & (1u << bar)) != 0
		&& !vega10RegisterBar) {
		return B_NOT_SUPPORTED;
	}
	if ((device->pci.header_type & PCI_header_type_mask)
		!= PCI_header_type_generic) {
		return B_NOT_SUPPORTED;
	}

	const uint8 flags = device->pci.u.h0.base_register_flags[bar];
	if ((flags & PCI_address_space) != 0)
		return B_BAD_VALUE;

	const uint8 type = flags & PCI_address_type;
	if (type != PCI_address_type_32 && type != PCI_address_type_32_low
		&& type != PCI_address_type_64) {
		return B_NOT_SUPPORTED;
	}
	if (type == PCI_address_type_64 && bar == 5)
		return B_BAD_DATA;

	const uint32 size = device->mmio_bar_size[bar];
	if (size < B_PAGE_SIZE || (size % B_PAGE_SIZE) != 0
		|| (size & (size - 1)) != 0) {
		return B_BAD_DATA;
	}

	uint64 physicalAddress = device->pci.u.h0.base_registers_pci[bar]
		& PCI_address_memory_32_mask;
	if (type == PCI_address_type_64) {
		physicalAddress |= (uint64)
			device->pci.u.h0.base_registers_pci[bar + 1] << 32;
	}
	if (physicalAddress == 0
		|| (physicalAddress & (B_PAGE_SIZE - 1)) != 0
		|| physicalAddress > (~(uint64)0) - size) {
		return B_BAD_DATA;
	}
	const phys_addr_t physicalAddressForMapping
		= (phys_addr_t)physicalAddress;
	if ((uint64)physicalAddressForMapping != physicalAddress)
		return B_NOT_SUPPORTED;

	void* mappedAddress = NULL;
	area_id area = map_physical_memory("amdgpu-mmio",
		physicalAddressForMapping, size,
		B_ANY_KERNEL_BLOCK_ADDRESS | B_UNCACHED_MEMORY,
		B_READ_AREA | B_WRITE_AREA, &mappedAddress);
	if (area < 0)
		return (status_t)area;
	if (mappedAddress == NULL) {
		// Preserve the area ID so release can still tear it down.
		resources->mmio_area[bar] = area;
		resources->mmio_physical[bar] = physicalAddressForMapping;
		resources->mmio_size[bar] = size;
		return B_NO_MEMORY;
	}

	resources->mmio_area[bar] = area;
	resources->mmio[bar] = mappedAddress;
	resources->mmio_physical[bar] = physicalAddressForMapping;
	resources->mmio_size[bar] = size;
	return B_OK;
}


status_t
amdgpu_unmap_mmio_bar(amdgpu_device_resources* resources, uint8 bar)
{
	if (resources == NULL || bar >= 6
		|| resources->magic != AMDGPU_DEVICE_RESOURCES_MAGIC)
		return B_BAD_VALUE;
	if (resources->mmio_area[bar] < 0)
		return B_OK;

	status_t status = delete_area(resources->mmio_area[bar]);
	if (status != B_OK)
		return status;

	resources->mmio_area[bar] = -1;
	resources->mmio[bar] = NULL;
	resources->mmio_physical[bar] = 0;
	resources->mmio_size[bar] = 0;
	return B_OK;
}


status_t
amdgpu_release_device(pci_module_info* pci,
	amdgpu_device_resources* resources)
{
	if (pci == NULL || resources == NULL
		|| resources->magic != AMDGPU_DEVICE_RESOURCES_MAGIC)
		return B_BAD_VALUE;

	for (int32 bar = 5; bar >= 0; bar--) {
		status_t status = amdgpu_unmap_mmio_bar(resources, (uint8)bar);
		if (status != B_OK)
			return status;
	}

	if (resources->reserved) {
		if (pci->unreserve_device == NULL)
			return B_NOT_SUPPORTED;
		status_t status = pci->unreserve_device(resources->pci.bus,
			resources->pci.device, resources->pci.function,
			kAMDGPUDriverName, resources);
		if (status != B_OK)
			return status;
	}

	InitializeResources(*resources);
	return B_OK;
}
