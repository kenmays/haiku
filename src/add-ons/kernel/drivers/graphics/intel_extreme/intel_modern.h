/*
 * Copyright 2026, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef INTEL_EXTREME_MODERN_H
#define INTEL_EXTREME_MODERN_H

#include <SupportDefs.h>

/*
 * Generation-level display-engine description.
 *
 * This layer deliberately describes capabilities rather than claiming that
 * every generation is fully modeset-capable.  The legacy accelerant remains
 * the policy layer; this table is the hardware capability boundary used by
 * later display code.
 */
enum intel_display_engine {
	INTEL_DISPLAY_ENGINE_LEGACY = 0,
	INTEL_DISPLAY_ENGINE_PCH,
	INTEL_DISPLAY_ENGINE_DDI,
	INTEL_DISPLAY_ENGINE_MODERN
};

struct intel_generation_info {
	uint8 generation;
	uint8 pipe_count;
	uint8 port_count;
	intel_display_engine display_engine;
	bool has_pch;
	bool has_ddi;
	bool has_aux;
	bool has_hotplug;
	bool has_hw_cursor;
	bool has_ring;
	bool has_blitter;
};

const intel_generation_info& intel_get_generation_info(uint8 generation);

#endif
