/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_REGISTER_APERTURE_H
#define AMDGPU_REGISTER_APERTURE_H

#include <SupportDefs.h>

#include "device_resources.h"
#include "pci_device.h"

/*
 * A register window is described by an ASIC-specific, reviewed BAR-relative
 * offset. Discovery base_addresses are intentionally not accepted here:
 * they are device register-space indices, not CPU physical addresses.
 */
struct amdgpu_register_aperture {
	uint8 bar;
	uint64 offset;
	uint64 size;
	volatile uint8* address;
};

status_t amdgpu_resolve_register_aperture(
	const amdgpu_pci_device* device,
	const amdgpu_device_resources* resources, uint8 bar,
	uint64 bar_relative_offset, uint64 aperture_size,
	amdgpu_register_aperture* aperture);

/* Validate one 32-bit register access without performing the access. */
status_t amdgpu_register_offset(const amdgpu_register_aperture* aperture,
	uint64 register_offset, uint32* mapped_offset);

#endif
