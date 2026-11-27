/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_PSP_FIRMWARE_PROVIDER_H
#define AMDGPU_PSP_FIRMWARE_PROVIDER_H

#include "amdgpu_psp_firmware.h"

static const uint32 AMDGPU_PSP_FIRMWARE_PROVIDER_MAGIC = 0x41505350;
static const uint32 AMDGPU_PSP_FIRMWARE_HAIKU_CONTEXT_MAGIC = 0x4150484B;

struct firmware;

/*
 * load() transfers one independently releasable buffer to the caller only
 * when it returns B_OK. On success, data must be non-NULL and size nonzero.
 * release() must accept exactly the pointer and size returned by load().
 * A failed load must not transfer ownership. Callbacks run in thread context.
 */
struct amdgpu_psp_firmware_haiku_context {
	uint32 magic;
	const firmware* sos_handle;
	const firmware* asd_handle;
};


struct amdgpu_psp_firmware_provider_ops {
	status_t (*load)(void* cookie, const char* name, const void** data,
		size_t* size);
	status_t (*release)(void* cookie, const void* data, size_t size);
};

/*
 * Caller-owned acquisition state. Keep this object and the bundle alive until
 * unload() succeeds. Failed releases remain recorded for a later retry.
 */
struct amdgpu_psp_firmware_provider {
	uint32 magic;
	amdgpu_psp_firmware_provider_ops ops;
	void* cookie;
	amdgpu_psp_firmware_bundle* bundle;
	const void* sos_data;
	size_t sos_size;
	const void* asd_data;
	size_t asd_size;
	bool sos_acquired;
	bool asd_acquired;
	bool ready;
};

void amdgpu_psp_firmware_provider_init(
	amdgpu_psp_firmware_provider* provider);
status_t amdgpu_psp_firmware_provider_load(
	amdgpu_psp_firmware_provider* provider,
	amdgpu_psp_firmware_bundle* bundle,
	const amdgpu_psp_firmware_provider_ops* ops, void* cookie,
	const amdgpu_psp_firmware_version_policy* policy);
status_t amdgpu_psp_firmware_provider_unload(
	amdgpu_psp_firmware_provider* provider);

void amdgpu_psp_firmware_haiku_context_init(
	amdgpu_psp_firmware_haiku_context* context);
status_t amdgpu_psp_firmware_provider_load_haiku(
	amdgpu_psp_firmware_provider* provider,
	amdgpu_psp_firmware_bundle* bundle,
	amdgpu_psp_firmware_haiku_context* context,
	const amdgpu_psp_firmware_version_policy* policy);

#endif
