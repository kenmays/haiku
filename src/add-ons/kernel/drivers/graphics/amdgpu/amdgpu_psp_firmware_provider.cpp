/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <string.h>

#include "amdgpu_psp_firmware_provider.h"

static const char* const kSOSFirmwareName = "amdgpu/vega10_sos.bin";
static const char* const kASDFirmwareName = "amdgpu/vega10_asd.bin";


void
amdgpu_psp_firmware_provider_init(amdgpu_psp_firmware_provider* provider)
{
	if (provider == NULL)
		return;
	memset(provider, 0, sizeof(*provider));
	provider->magic = AMDGPU_PSP_FIRMWARE_PROVIDER_MAGIC;
}


static status_t
ReleaseAcquired(amdgpu_psp_firmware_provider* provider)
{
	status_t firstError = B_OK;

	/* Release in reverse acquisition order and retain failed ownership. */
	if (provider->asd_acquired) {
		status_t status = provider->ops.release(provider->cookie,
			provider->asd_data, provider->asd_size);
		if (status == B_OK) {
			provider->asd_acquired = false;
			provider->asd_data = NULL;
			provider->asd_size = 0;
		} else
			firstError = status;
	}

	if (provider->sos_acquired) {
		status_t status = provider->ops.release(provider->cookie,
			provider->sos_data, provider->sos_size);
		if (status == B_OK) {
			provider->sos_acquired = false;
			provider->sos_data = NULL;
			provider->sos_size = 0;
		} else if (firstError == B_OK)
			firstError = status;
	}

	return firstError;
}


status_t
amdgpu_psp_firmware_provider_unload(amdgpu_psp_firmware_provider* provider)
{
	if (provider == NULL
		|| provider->magic != AMDGPU_PSP_FIRMWARE_PROVIDER_MAGIC)
		return B_BAD_VALUE;

	provider->ready = false;
	if (provider->bundle != NULL) {
		status_t status = amdgpu_psp_firmware_bundle_unload(provider->bundle);
		if (status != B_OK)
			return status;
		provider->bundle = NULL;
	}

	status_t status = ReleaseAcquired(provider);
	if (status != B_OK)
		return status;

	memset(&provider->ops, 0, sizeof(provider->ops));
	provider->cookie = NULL;
	return B_OK;
}


status_t
amdgpu_psp_firmware_provider_load(amdgpu_psp_firmware_provider* provider,
	amdgpu_psp_firmware_bundle* bundle,
	const amdgpu_psp_firmware_provider_ops* ops, void* cookie,
	const amdgpu_psp_firmware_version_policy* policy)
{
	if (provider == NULL
		|| provider->magic != AMDGPU_PSP_FIRMWARE_PROVIDER_MAGIC
		|| bundle == NULL || ops == NULL || ops->load == NULL
		|| ops->release == NULL || policy == NULL
		|| bundle->magic != AMDGPU_PSP_FIRMWARE_BUNDLE_MAGIC)
		return B_BAD_VALUE;

	if (provider->bundle != NULL || provider->sos_acquired
		|| provider->asd_acquired || provider->ready || bundle->loaded)
		return B_BUSY;

	provider->ops = *ops;
	provider->cookie = cookie;
	provider->bundle = bundle;

	status_t status = provider->ops.load(provider->cookie,
		kSOSFirmwareName, &provider->sos_data, &provider->sos_size);
	if (status != B_OK)
		goto fail;
	provider->sos_acquired = true;
	if (provider->sos_data == NULL || provider->sos_size == 0) {
		status = B_BAD_DATA;
		goto fail;
	}

	status = provider->ops.load(provider->cookie, kASDFirmwareName,
		&provider->asd_data, &provider->asd_size);
	if (status != B_OK)
		goto fail;
	provider->asd_acquired = true;
	if (provider->asd_data == NULL || provider->asd_size == 0) {
		status = B_BAD_DATA;
		goto fail;
	}

	status = amdgpu_psp_firmware_bundle_load(bundle, provider->sos_data,
		provider->sos_size, provider->asd_data, provider->asd_size);
	if (status != B_OK)
		goto fail;

	status = amdgpu_psp_firmware_bundle_check_versions(bundle, policy);
	if (status != B_OK)
		goto fail;

	provider->ready = true;
	return B_OK;

fail:
	/*
	 * Keep the provider/bundle bookkeeping if a release fails. The caller
	 * must retry unload() before reusing either object.
	 */
	{
		status_t cleanupStatus = amdgpu_psp_firmware_provider_unload(
			provider);
		return cleanupStatus != B_OK ? cleanupStatus : status;
	}
}
