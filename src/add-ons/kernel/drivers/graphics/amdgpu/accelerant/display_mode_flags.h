/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AMDGPU accelerant display-mode flag helpers.
 */
#ifndef AMDGPU_DISPLAY_MODE_FLAGS_H
#define AMDGPU_DISPLAY_MODE_FLAGS_H

#include <Accelerant.h>
#include <SupportDefs.h>

/*
 * B_PARALLEL_ACCESS permits parallel framebuffer access (for example,
 * BDirectWindow). Set it only when the accelerant can guarantee safe
 * concurrent CPU/framebuffer access for the active scanout mapping.
 *
 * The current PCI-only driver has no accelerant or framebuffer mapping yet;
 * callers must pass false until those facilities are implemented and tested.
 */
static inline uint32
amdgpu_set_parallel_access_flag(uint32 flags, bool supported)
{
	if (supported)
		return flags | B_PARALLEL_ACCESS;
	return flags & ~B_PARALLEL_ACCESS;
}

#endif
