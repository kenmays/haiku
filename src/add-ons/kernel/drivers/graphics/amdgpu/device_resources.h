/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_DEVICE_RESOURCES_H
#define AMDGPU_DEVICE_RESOURCES_H

#include <KernelExport.h>
#include <PCI.h>
#include <SupportDefs.h>

#include "pci_device.h"

static const uint32 AMDGPU_DEVICE_RESOURCES_MAGIC = 0x414d4452;

struct amdgpu_device_resources {
	uint32 magic;
	pci_info pci;
	bool reserved;
	area_id mmio_area[6];
	void* mmio[6];
	phys_addr_t mmio_physical[6];
	size_t mmio_size[6];
};

/*
 * Injectable OS mapping operations. Production callers use the native
 * wrappers below; tests can supply deterministic failures without touching
 * physical memory. The table is borrowed for the duration of one call.
 */
struct amdgpu_resource_mapping_ops {
	area_id (*map)(const char* name, phys_addr_t physical_address,
		size_t size, uint32 address_specification, uint32 protection,
		void** mapped_address);
	status_t (*unmap)(area_id area);
};

/* Zero-initialize storage before first use. Reinitialize only after release. */
status_t amdgpu_init_device_resources(amdgpu_device_resources* resources);
status_t amdgpu_claim_device(pci_module_info* pci,
	const amdgpu_pci_device* device, amdgpu_device_resources* resources);
status_t amdgpu_map_mmio_bar(const amdgpu_pci_device* device,
	amdgpu_device_resources* resources, uint8 bar);
status_t amdgpu_map_mmio_bar_with_ops(const amdgpu_pci_device* device,
	amdgpu_device_resources* resources, uint8 bar,
	const amdgpu_resource_mapping_ops* ops);
status_t amdgpu_unmap_mmio_bar(amdgpu_device_resources* resources,
	uint8 bar);
status_t amdgpu_unmap_mmio_bar_with_ops(amdgpu_device_resources* resources,
	uint8 bar, const amdgpu_resource_mapping_ops* ops);
status_t amdgpu_release_device(pci_module_info* pci,
	amdgpu_device_resources* resources);

#endif
