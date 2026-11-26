/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VEGA10_PROBE_H
#define AMDGPU_VEGA10_PROBE_H

#include <SupportDefs.h>

#include "amdgpu_vega10_mmio.h"

/*
 * Low-level unlocked read helper for the Vega 10 GC GB_ADDR_CONFIG register.
 * Call only while holding the owning device-context mutex; the context API
 * provides this serialization. This performs one whitelisted 32-bit read.
 */
status_t amdgpu_vega10_read_gb_addr_config_unlocked(
	const amdgpu_pci_device* device,
	const amdgpu_device_resources* resources,
	const amdgpu_ip_discovery_info* discovery, uint32* value);

#endif
