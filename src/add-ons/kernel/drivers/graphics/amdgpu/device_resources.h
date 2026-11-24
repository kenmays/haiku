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

struct amdgpu_device_resources {
	pci_info pci;
	bool reserved;
	area_id mmio_area[6];
	void* mmio[6];
	phys_addr_t mmio_physical[6];
	size_t mmio_size[6];
};

void amdgpu_init_device_resources(amdgpu_device_resources* resources);
status_t amdgpu_claim_device(pci_module_info* pci,
	const amdgpu_pci_device* device, amdgpu_device_resources* resources);
status_t amdgpu_map_mmio_bar(const amdgpu_pci_device* device,
	amdgpu_device_resources* resources, uint8 bar);
status_t amdgpu_unmap_mmio_bar(amdgpu_device_resources* resources,
	uint8 bar);
status_t amdgpu_release_device(pci_module_info* pci,
	amdgpu_device_resources* resources);

#endif
