#include "rdna4_frontend.h"

#include <string.h>

RDNA4Frontend::RDNA4Frontend()
	: fOps(NULL), fContext(NULL)
{
	memset(&fInfo, 0, sizeof(fInfo));
	memset(fConnectors, 0, sizeof(fConnectors));
	fInfo.version = 1;
	fInfo.state = RDNA4_FRONTEND_NEW;
	fInfo.dpms_mode = B_DPMS_OFF;
}

status_t
RDNA4Frontend::Initialize(const rdna4_frontend_ops* ops, void* context)
{
	if (ops == NULL || ops->attach == NULL || context == NULL)
		return B_BAD_VALUE;
	if (fInfo.state != RDNA4_FRONTEND_NEW)
		return B_BUSY;

	rdna4_frontend_info info;
	memset(&info, 0, sizeof(info));
	info.version = 1;
	info.dpms_mode = B_DPMS_OFF;

	status_t status = ops->attach(context, info);
	if (status != B_OK) {
		fInfo.state = RDNA4_FRONTEND_FAILED;
		return status;
	}
	if (info.connector_count > 8)
		return B_BAD_VALUE;

	fOps = ops;
	fContext = context;
	fInfo = info;
	fInfo.state = RDNA4_FRONTEND_ATTACHED;

	for (uint32 i = 0; i < fInfo.connector_count; i++) {
		fConnectors[i].id = i;
		fConnectors[i].connected = false;
		fConnectors[i].has_edid = false;
		fConnectors[i].mode_count = 0;
	}

	return B_OK;
}

status_t
RDNA4Frontend::RefreshModes(uint32 connector)
{
	if (!IsAttached() || fOps == NULL || fOps->refresh_modes == NULL)
		return B_NOT_SUPPORTED;
	if (connector >= fInfo.connector_count)
		return B_BAD_INDEX;

	status_t status = fOps->refresh_modes(fContext, connector);
	if (status != B_OK)
		return status;

	if (fConnectors[connector].mode_count != 0)
		fInfo.state = RDNA4_FRONTEND_MODES_READY;
	return B_OK;
}

status_t
RDNA4Frontend::SetMode(uint32 connector, const display_mode& mode,
	uint64 framebuffer)
{
	if (!IsAttached() || fOps == NULL || fOps->set_mode == NULL)
		return B_NOT_SUPPORTED;
	if (connector >= fInfo.connector_count)
		return B_BAD_INDEX;
	if (framebuffer == 0)
		return B_BAD_VALUE;

	status_t status = fOps->set_mode(fContext, connector, mode, framebuffer);
	if (status != B_OK)
		return status;

	fInfo.active_connector = connector;
	fInfo.state = RDNA4_FRONTEND_ACTIVE;
	fInfo.framebuffer_valid = true;
	return B_OK;
}

status_t
RDNA4Frontend::SetDPMS(uint32 mode)
{
	if (!IsAttached() || fOps == NULL || fOps->set_dpms == NULL)
		return B_NOT_SUPPORTED;
	if (mode != B_DPMS_ON && mode != B_DPMS_STAND_BY
		&& mode != B_DPMS_SUSPEND && mode != B_DPMS_OFF)
		return B_BAD_VALUE;

	status_t status = fOps->set_dpms(fContext, mode);
	if (status != B_OK)
		return status;

	fInfo.dpms_mode = mode;
	if (mode == B_DPMS_OFF)
		fInfo.state = RDNA4_FRONTEND_SUSPENDED;
	else if (fInfo.framebuffer_valid)
		fInfo.state = RDNA4_FRONTEND_ACTIVE;
	return B_OK;
}

status_t
RDNA4Frontend::GetEDID(uint32 connector, void* buffer, size_t size,
	size_t* _actual)
{
	if (!IsAttached() || fOps == NULL || fOps->get_edid == NULL)
		return B_NOT_SUPPORTED;
	if (connector >= fInfo.connector_count || buffer == NULL || size == 0
		|| _actual == NULL)
		return B_BAD_VALUE;
	return fOps->get_edid(fContext, connector, buffer, size, _actual);
}

status_t
RDNA4Frontend::Uninitialize()
{
	if (fInfo.state == RDNA4_FRONTEND_NEW)
		return B_OK;

	status_t status = B_OK;
	if (fOps != NULL && fOps->detach != NULL)
		status = fOps->detach(fContext);

	fOps = NULL;
	fContext = NULL;
	memset(&fInfo, 0, sizeof(fInfo));
	memset(fConnectors, 0, sizeof(fConnectors));
	fInfo.version = 1;
	fInfo.state = RDNA4_FRONTEND_NEW;
	fInfo.dpms_mode = B_DPMS_OFF;
	return status;
}

const rdna4_frontend_connector*
RDNA4Frontend::Connector(uint32 index) const
{
	if (index >= fInfo.connector_count || index >= 8)
		return NULL;
	return &fConnectors[index];
}
