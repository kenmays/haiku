/*
 * Intel Extreme modernization support.
 *
 * Copyright 2026, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef INTEL_EXTREME_MODERN_H
#define INTEL_EXTREME_MODERN_H

#include <SupportDefs.h>

struct intel_modern_device {
	uint16 device_id;
	uint8 generation;
	uint8 display_pipes;
	uint8 transcoders;
	uint8 gt_units;
	uint32 flags;
	const char* name;
};

enum intel_modern_generation {
	INTEL_GEN_LEGACY = 0,
	INTEL_GEN_6,
	INTEL_GEN_7,
	INTEL_GEN_7_5,
	INTEL_GEN_8,
	INTEL_GEN_9,
	INTEL_GEN_11,
	INTEL_GEN_12
};

enum intel_modern_flags {
	INTEL_MODERN_HAS_GTT = 1u << 0,
	INTEL_MODERN_HAS_PCH = 1u << 1,
	INTEL_MODERN_HAS_MSI = 1u << 2,
	INTEL_MODERN_HAS_VBLANK = 1u << 3,
	INTEL_MODERN_HAS_DP = 1u << 4,
	INTEL_MODERN_HAS_EDP = 1u << 5,
	INTEL_MODERN_EXPERIMENTAL = 1u << 31
};

const intel_modern_device* intel_modern_lookup(uint16 deviceID);
const char* intel_modern_generation_name(uint8 generation);
bool intel_modern_has_capability(const intel_modern_device& device, uint32 flag);

#endif
