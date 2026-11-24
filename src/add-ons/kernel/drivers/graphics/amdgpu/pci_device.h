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

enum amdgpu_asic_family {
	AMDGPU_ASIC_UNKNOWN = 0,
	AMDGPU_ASIC_TAHITI,
	AMDGPU_ASIC_BONAIRE,
	AMDGPU_ASIC_HAWAII,
	AMDGPU_ASIC_POLARIS10,
	AMDGPU_ASIC_VEGA10,
	AMDGPU_ASIC_VEGA20,
	AMDGPU_ASIC_NAVI10,
	AMDGPU_ASIC_NAVI21,
	AMDGPU_ASIC_NAVI31
};

enum {
	AMDGPU_GENERATION_UNKNOWN = 0,
	AMDGPU_GENERATION_GCN = 1,
	AMDGPU_GENERATION_RDNA1 = 2,
	AMDGPU_GENERATION_RDNA2 = 3,
	AMDGPU_GENERATION_RDNA3 = 4
};

struct amdgpu_pci_device {
	pci_info pci;
	uint8 mmio_bar_mask;
	uint8 prefetchable_bar_mask;
	uint8 reserved[2];
	uint16 asic_family;
	uint8 generation;
	uint8 identification_flags;
	uint32 mmio_bar_size[6];
};

status_t amdgpu_enumerate(pci_module_info* pci,
	amdgpu_pci_device* devices, uint32 capacity, uint32* count);

#endif
