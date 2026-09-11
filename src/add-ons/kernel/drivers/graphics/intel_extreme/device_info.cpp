/*
 * Copyright 2026, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include "device_info.h"


intel_device_profile
intel_get_device_profile(uint16 deviceID, DeviceType type, const char* name)
{
	const uint8 generation = (uint8)type.Generation();
	intel_device_profile profile = {
		deviceID,
		type,
		generation,
		2,
		4,
		0,
		name
	};

	/* Pipe topology follows the display engine rather than the PCI ID list. */
	if (generation >= 12) {
		profile.pipe_count = 4;
		profile.port_count = 7;
	} else if (generation >= 7) {
		profile.pipe_count = 3;
		profile.port_count = 6;
	}

	if (generation < 8)
		profile.capabilities |= INTEL_CAP_LEGACY_DISPLAY;
	else
		profile.capabilities |= INTEL_CAP_DDI | INTEL_CAP_AUX
			| INTEL_CAP_HOTPLUG;

	if (generation >= 6)
		profile.capabilities |= INTEL_CAP_HW_CURSOR | INTEL_CAP_RING;

	if (generation >= 8)
		profile.capabilities |= INTEL_CAP_BLITTER;

	if (generation >= 5 && generation <= 7)
		profile.capabilities |= INTEL_CAP_PCH_DISPLAY;

	return profile;
}
