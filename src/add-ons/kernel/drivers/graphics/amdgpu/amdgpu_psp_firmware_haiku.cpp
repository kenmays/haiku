/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Native Haiku firmware-filesystem adapter for the generic PSP provider.
 */
#include <string.h>

#include <compat/sys/firmware.h>
#include <compat/sys/haiku-module.h>

#include "amdgpu_psp_firmware_provider.h"


/*
 * firmware_get() resolves files below
 * <system-data>/firmware/<gDriverName>/<mapped-name>.
 */
extern "C" {
const char* gDriverName = "amdgpu";
HAIKU_FIRMWARE_VERSION(0);
HAIKU_FIRMWARE_NAME_MAP({
	{ "vega10_sos.bin", "vega10_sos.bin" },
	{ "vega10_asd.bin", "vega10_asd.bin" }
});
}


static const size_t kMaximumFirmwareFileSize = 16 * 1024 * 1024;


void
amdgpu_psp_firmware_haiku_context_init(
	amdgpu_psp_firmware_haiku_context* context)
{
	if (context == NULL)
		return;

	memset(context, 0, sizeof(*context));
	context->magic = AMDGPU_PSP_FIRMWARE_HAIKU_CONTEXT_MAGIC;
}


static status_t
LoadHaikuFirmware(void* cookie, const char* name, const void** data,
	size_t* size)
{
	amdgpu_psp_firmware_haiku_context* context
		= (amdgpu_psp_firmware_haiku_context*)cookie;
	if (context == NULL
		|| context->magic != AMDGPU_PSP_FIRMWARE_HAIKU_CONTEXT_MAGIC
		|| name == NULL || data == NULL || size == NULL)
		return B_BAD_VALUE;

	*data = NULL;
	*size = 0;

	const void** handleSlot = NULL;
	if (strcmp(name, "vega10_sos.bin") == 0)
		handleSlot = &context->sos_handle;
	else if (strcmp(name, "vega10_asd.bin") == 0)
		handleSlot = &context->asd_handle;
	else
		return B_ENTRY_NOT_FOUND;

	if (*handleSlot != NULL)
		return B_BUSY;

	const struct firmware* firmware = firmware_get(name);
	if (firmware == NULL)
		return B_ENTRY_NOT_FOUND;

	if (firmware->data == NULL || firmware->datasize == 0
		|| firmware->datasize > kMaximumFirmwareFileSize) {
		firmware_put(firmware, FIRMWARE_UNLOAD);
		return B_BAD_DATA;
	}

	*handleSlot = firmware;
	*data = firmware->data;
	*size = firmware->datasize;
	return B_OK;
}


static status_t
ReleaseHaikuFirmware(void* cookie, const void* data, size_t size)
{
	amdgpu_psp_firmware_haiku_context* context
		= (amdgpu_psp_firmware_haiku_context*)cookie;
	if (context == NULL
		|| context->magic != AMDGPU_PSP_FIRMWARE_HAIKU_CONTEXT_MAGIC
		|| data == NULL || size == 0)
		return B_BAD_VALUE;

	const struct firmware** handleSlot = NULL;
	if (context->asd_handle != NULL) {
		const struct firmware* firmware
			= (const struct firmware*)context->asd_handle;
		if (firmware->data == data && firmware->datasize == size)
			handleSlot = (const struct firmware**)&context->asd_handle;
	}
	if (handleSlot == NULL && context->sos_handle != NULL) {
		const struct firmware* firmware
			= (const struct firmware*)context->sos_handle;
		if (firmware->data == data && firmware->datasize == size)
			handleSlot = (const struct firmware**)&context->sos_handle;
	}
	if (handleSlot == NULL)
		return B_BAD_VALUE;

	const struct firmware* firmware = *handleSlot;
	*handleSlot = NULL;
	firmware_put(firmware, FIRMWARE_UNLOAD);
	return B_OK;
}


status_t
amdgpu_psp_firmware_provider_load_haiku(
	amdgpu_psp_firmware_provider* provider,
	amdgpu_psp_firmware_bundle* bundle,
	amdgpu_psp_firmware_haiku_context* context,
	const amdgpu_psp_firmware_version_policy* policy)
{
	if (context == NULL
		|| context->magic != AMDGPU_PSP_FIRMWARE_HAIKU_CONTEXT_MAGIC
		|| context->sos_handle != NULL || context->asd_handle != NULL)
		return B_BAD_VALUE;

	const amdgpu_psp_firmware_provider_ops ops = {
		LoadHaikuFirmware,
		ReleaseHaikuFirmware
	};
	return amdgpu_psp_firmware_provider_load(provider, bundle, &ops,
		context, policy);
}
