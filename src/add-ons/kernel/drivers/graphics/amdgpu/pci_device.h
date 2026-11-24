/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_PCI_DEVICE_H
#define AMDGPU_PCI_DEVICE_H

#include <PCI.h>
#include <SupportDefs.h>

static const uint32 AMDGPU_MAX_DEVICES = 16;
static const uint32 AMDGPU_MAX_PCI_FUNCTIONS = 256;

struct amdgpu_pci_device {
	pci_info pci;
	uint8 mmio_bar_mask;
	uint8 prefetchable_bar_mask;
	uint8 reserved[2];
	uint32 mmio_bar_size[6];
};

status_t amdgpu_enumerate(pci_module_info* pci,
	amdgpu_pci_device* devices, uint32 capacity, uint32* count);

#endif
