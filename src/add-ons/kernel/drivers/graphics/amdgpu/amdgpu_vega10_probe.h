/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_VEGA10_PROBE_H
#define AMDGPU_VEGA10_PROBE_H

#include <SupportDefs.h>

#include "amdgpu_vega10_mmio.h"

/*
 * Read the Vega 10 GC GB_ADDR_CONFIG register. This is a single, aligned,
 * whitelisted 32-bit MMIO read. No register writes or indirect/indexed
 * accesses are performed. Caller owns PCI reservation and BAR mapping.
 */
status_t amdgpu_vega10_read_gb_addr_config_unlocked(
	const amdgpu_pci_device* device,
	const amdgpu_device_resources* resources,
	const amdgpu_ip_discovery_info* discovery, uint32* value);

#endif
