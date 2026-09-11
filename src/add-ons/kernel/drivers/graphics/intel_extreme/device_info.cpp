/*
 * Copyright 2026, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include "device_info.h"


intel_device_profile
intel_get_device_profile(uint16 deviceID, DeviceType type, const char* name)
{
	const uint8 generation = (uint8)type.Generation();
	const intel_generation_info& generationInfo
		= intel_get_generation_info(generation);

	intel_device_profile profile = {
		deviceID,
		type,
		generation,
		generationInfo.pipe_count,
		generationInfo.port_count,
		0,
		name
	};

	if (generationInfo.display_engine == INTEL_DISPLAY_ENGINE_LEGACY)
		profile.capabilities |= INTEL_CAP_LEGACY_DISPLAY;

	if (generationInfo.has_ddi)
		profile.capabilities |= INTEL_CAP_DDI;

	if (generationInfo.has_aux)
		profile.capabilities |= INTEL_CAP_AUX;

	if (generationInfo.has_hotplug)
		profile.capabilities |= INTEL_CAP_HOTPLUG;

	if (generationInfo.has_hw_cursor)
		profile.capabilities |= INTEL_CAP_HW_CURSOR;

	if (generationInfo.has_ring)
		profile.capabilities |= INTEL_CAP_RING;

	if (generationInfo.has_blitter)
		profile.capabilities |= INTEL_CAP_BLITTER;

	if (generationInfo.has_pch)
		profile.capabilities |= INTEL_CAP_PCH_DISPLAY;

	return profile;
}
