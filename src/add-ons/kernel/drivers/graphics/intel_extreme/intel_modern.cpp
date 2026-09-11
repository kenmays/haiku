/*
 * Copyright 2026, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include "intel_modern.h"

namespace {

const intel_generation_info kUnknown = {
	0, 1, 1, INTEL_DISPLAY_ENGINE_LEGACY,
	false, false, false, false, false, false, false
};

/*
 * Conservative topology/capability defaults.  The table intentionally
 * avoids per-device assumptions: PCI IDs select the device family while
 * this layer selects the display-engine generation.
 */
const intel_generation_info kGenerations[] = {
	{1, 1, 2, INTEL_DISPLAY_ENGINE_LEGACY, false, false, false, false,
		false, false, false},
	{2, 2, 2, INTEL_DISPLAY_ENGINE_LEGACY, false, false, false, false,
		false, false, false},
	{3, 2, 2, INTEL_DISPLAY_ENGINE_LEGACY, false, false, false, false,
		false, false, false},
	{4, 2, 2, INTEL_DISPLAY_ENGINE_LEGACY, false, false, false, false,
		false, false, false},
	{5, 2, 2, INTEL_DISPLAY_ENGINE_PCH, true, false, false, true,
		false, false, false},
	{6, 2, 3, INTEL_DISPLAY_ENGINE_PCH, true, false, false, true,
		true, true, false},
	{7, 3, 4, INTEL_DISPLAY_ENGINE_PCH, true, false, false, true,
		true, true, false},
	{8, 3, 6, INTEL_DISPLAY_ENGINE_DDI, true, true, true, true,
		true, true, true},
	{9, 3, 6, INTEL_DISPLAY_ENGINE_DDI, true, true, true, true,
		true, true, true},
	{10, 3, 6, INTEL_DISPLAY_ENGINE_DDI, true, true, true, true,
		true, true, true},
	{11, 3, 6, INTEL_DISPLAY_ENGINE_MODERN, false, true, true, true,
		true, true, true},
	{12, 4, 7, INTEL_DISPLAY_ENGINE_MODERN, false, true, true, true,
		true, true, true}
};

} // namespace


const intel_generation_info&
intel_get_generation_info(uint8 generation)
{
	if (generation == 0 || generation > 12)
		return kUnknown;

	return kGenerations[generation - 1];
}
