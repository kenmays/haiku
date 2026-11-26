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
 * The SOC15 common path reserves the final page of the first 0x80000 bytes
 * as a special MMIO register hole.
 */
static const uint8 AMDGPU_VEGA10_REGISTER_BAR = 5;
static const uint32 AMDGPU_VEGA10_MMIO_HOLE_OFFSET = 0x7f000;
static const uint32 AMDGPU_VEGA10_MMIO_HOLE_SIZE = 0x1000;
static const uint32 AMDGPU_VEGA10_MMIO_MIN_SIZE = 0x80000;

/*
 * Resolve the complete mapped BAR 5 aperture for a validated Vega 10 device.
 * This only validates metadata and returns a pointer/window; it performs no
 * MMIO access. The caller must already have claimed and mapped BAR 5.
 */
status_t amdgpu_vega10_resolve_register_aperture(
	const amdgpu_pci_device* device,
	const amdgpu_device_resources* resources,
	const amdgpu_vega10_profile* profile,
	amdgpu_register_aperture* aperture);

/*
 * Convert a byte offset from the SOC15 register-window origin into an offset
 * within the mapped BAR 5 aperture. The special hole and every offset after
 * it are rejected until the SOC15 remap mechanism is implemented.
 */
status_t amdgpu_vega10_register_mapped_offset(
	const amdgpu_register_aperture* aperture, uint64 registerByteOffset,
	uint32* mappedOffset);

#endif
