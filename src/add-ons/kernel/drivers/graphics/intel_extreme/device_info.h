/*
 * Copyright 2026, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef INTEL_EXTREME_DEVICE_INFO_H
#define INTEL_EXTREME_DEVICE_INFO_H

#include <SupportDefs.h>

#include "intel_extreme.h"
#include "intel_modern.h"


enum intel_device_capability {
	INTEL_CAP_LEGACY_DISPLAY = 1 << 0,
	INTEL_CAP_DDI = 1 << 1,
	INTEL_CAP_AUX = 1 << 2,
	INTEL_CAP_HOTPLUG = 1 << 3,
	INTEL_CAP_HW_CURSOR = 1 << 4,
	INTEL_CAP_RING = 1 << 5,
	INTEL_CAP_BLITTER = 1 << 6,
	INTEL_CAP_PCH_DISPLAY = 1 << 7
};


struct intel_device_profile {
	uint16 device_id;
	DeviceType type;
	uint8 generation;
	uint8 pipe_count;
	uint8 port_count;
	uint32 capabilities;
	const char* name;
};


intel_device_profile intel_get_device_profile(uint16 deviceID,
	DeviceType type, const char* name);

#endif
