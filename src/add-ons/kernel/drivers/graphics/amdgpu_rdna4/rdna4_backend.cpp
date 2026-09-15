#include "rdna4_backend.h"

#include <string.h>

RDNA4Backend::RDNA4Backend()
	: fOps(NULL), fHardwareOps(NULL), fContext(NULL)
{
	memset(&fInfo, 0, sizeof(fInfo));
	fInfo.state = RDNA4_BACKEND_NEW;
}

status_t
RDNA4Backend::Initialize(const rdna4_backend_ops* ops, void* context)
{
	if (ops == NULL || ops->discover == NULL || context == NULL)
		return B_BAD_VALUE;

	fOps = ops;
	fContext = context;
	memset(&fInfo, 0, sizeof(fInfo));

	status_t status = fOps->discover(fContext, fInfo);
	if (status != B_OK) {
		fInfo.state = RDNA4_BACKEND_FAILED;
		return status;
	}

	fInfo.state = RDNA4_BACKEND_DISCOVERED;
	return B_OK;
}

status_t
RDNA4Backend::Start()
{
	if (fOps == NULL || fContext == NULL)
		return B_NO_INIT;
	if (fInfo.state == RDNA4_BACKEND_FAILED)
		return B_ERROR;

	status_t status = B_OK;
	if (fOps->load_firmware != NULL) {
		status = fOps->load_firmware(fContext);
		if (status != B_OK)
			goto fail;
	}
	fInfo.state = RDNA4_BACKEND_FIRMWARE_READY;

	if (fOps->init_memory != NULL) {
		status = fOps->init_memory(fContext);
		if (status != B_OK)
			goto fail;
	}
	fInfo.state = RDNA4_BACKEND_MEMORY_READY;

	if (fOps->init_engines != NULL) {
		status = fOps->init_engines(fContext);
		if (status != B_OK)
			goto fail;
	}
	fInfo.state = RDNA4_BACKEND_ENGINES_READY;

	if (fOps->init_display != NULL) {
		status = fOps->init_display(fContext);
		if (status != B_OK)
			goto fail;
	}
	fInfo.state = RDNA4_BACKEND_DISPLAY_READY;

	if (fOps->start != NULL) {
		status = fOps->start(fContext);
		if (status != B_OK)
			goto fail;
	}
	fInfo.state = RDNA4_BACKEND_RUNNING;
	return B_OK;

fail:
	fInfo.state = RDNA4_BACKEND_FAILED;
	return status;
}

status_t
RDNA4Backend::Stop()
{
	if (fOps == NULL || fContext == NULL)
		return B_NO_INIT;
	if (fOps->stop == NULL)
		return B_NOT_SUPPORTED;

	status_t status = fOps->stop(fContext);
	if (status == B_OK)
		fInfo.state = RDNA4_BACKEND_DISCOVERED;
	return status;
}

status_t
RDNA4Backend::Reset()
{
	if (fHardwareOps == NULL)
		return B_NOT_SUPPORTED;
	return rdna4_hw_reset(fHardwareOps, fContext);
}
