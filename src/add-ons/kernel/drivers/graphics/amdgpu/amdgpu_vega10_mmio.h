/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VEGA10_MMIO_H
#define AMDGPU_VEGA10_MMIO_H

#include <SupportDefs.h>

#include "amdgpu_register_aperture.h"
#include "amdgpu_vega10_profile.h"

/*
 * Upstream SOC15 maps register MMIO from BAR 5 for Bonaire and newer ASICs.
 * SOC15 also defines a separate KFD userspace MMIO remap page at 0x7f000;
 * this does not interrupt the kernel's linear register aperture.
 */
static const uint8 AMDGPU_VEGA10_REGISTER_BAR = 5;
/*
 * Linux's rmmio_remap page is a separate KFD userspace mapping facility,
 * not a hole in the kernel's linear BAR register mapping.
 */
static const uint32 AMDGPU_VEGA10_KFD_MMIO_REMAP_OFFSET = 0x7f000;
static const uint32 AMDGPU_VEGA10_KFD_MMIO_REMAP_SIZE = 0x1000;
static const uint32 AMDGPU_VEGA10_GB_ADDR_CONFIG_OFFSET = 0x98f8;
static const uint32 AMDGPU_VEGA10_MMIO_MIN_SIZE = 0x10000;

/*
 * Resolve the complete mapped BAR 5 aperture for a Vega 10 device whose complete IP-discovery topology is revalidated.
 * This only validates metadata and returns a pointer/window; it performs no
 * MMIO access. The caller must already have claimed and mapped BAR 5.
 */
status_t amdgpu_vega10_resolve_register_aperture(
	const amdgpu_pci_device* device,
	const amdgpu_device_resources* resources,
	const amdgpu_ip_discovery_info* discovery,
	amdgpu_register_aperture* aperture);

/*
 * Convert a byte offset from the SOC15 register-window origin into an offset
 * within the mapped BAR 5 aperture. The mapping is linear for kernel register
 * access; KFD's separate rmmio_remap page is not a hole in this aperture.
 */
status_t amdgpu_vega10_register_mapped_offset(
	const amdgpu_register_aperture* aperture, uint64 registerByteOffset,
	uint32* mappedOffset);

#endif
