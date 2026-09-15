#include "rdna4_frontend_manager.h"

#include <string.h>

RDNA4FrontendManager::RDNA4FrontendManager()
	: fFrontend(NULL), fCurrentConnector(0), fFramebuffer(0),
	  fHasCurrentMode(false)
{
	memset(fModes, 0, sizeof(fModes));
	memset(fModeCounts, 0, sizeof(fModeCounts));
	memset(&fCurrentMode, 0, sizeof(fCurrentMode));
}

status_t
RDNA4FrontendManager::Initialize(RDNA4Frontend* frontend)
{
	if (frontend == NULL || !frontend->IsAttached())
		return B_BAD_VALUE;
	fFrontend = frontend;
	fFramebuffer = 0;
	fHasCurrentMode = false;
	memset(fModeCounts, 0, sizeof(fModeCounts));
	return B_OK;
}

status_t
RDNA4FrontendManager::Refresh(uint32 connector)
{
	if (fFrontend == NULL)
		return B_NO_INIT;
	if (connector >= fFrontend->Info().connector_count || connector >= 8)
		return B_BAD_INDEX;

	status_t status = fFrontend->RefreshModes(connector);
	if (status != B_OK)
		return status;

	/* The hardware frontend supplies the connector's validated mode count.
	 * Actual mode records are copied in by a future EDID/DCN provider. */
	const rdna4_frontend_connector* c = fFrontend->Connector(connector);
	if (c == NULL)
		return B_BAD_INDEX;
	fModeCounts[connector] = c->mode_count;
	return B_OK;
}

status_t
RDNA4FrontendManager::SetMode(uint32 connector, const display_mode& mode,
	uint64 framebuffer)
{
	if (fFrontend == NULL)
		return B_NO_INIT;
	if (framebuffer == 0)
		return B_BAD_VALUE;

	status_t status = fFrontend->SetMode(connector, mode, framebuffer);
	if (status != B_OK)
		return status;

	fCurrentConnector = connector;
	fFramebuffer = framebuffer;
	fCurrentMode = mode;
	fHasCurrentMode = true;
	return B_OK;
}

status_t
RDNA4FrontendManager::SetDPMS(uint32 mode)
{
	if (fFrontend == NULL)
		return B_NO_INIT;
	return fFrontend->SetDPMS(mode);
}

status_t
RDNA4FrontendManager::GetModeList(uint32 connector, display_mode* modes,
	uint32 capacity, uint32* _count) const
{
	if (_count == NULL || fFrontend == NULL)
		return B_BAD_VALUE;
	if (connector >= 8 || connector >= fFrontend->Info().connector_count)
		return B_BAD_INDEX;
	*_count = fModeCounts[connector];
	if (modes == NULL)
		return B_OK;
	if (capacity < *_count)
		return B_BUFFER_OVERFLOW;
	for (uint32 i = 0; i < *_count; i++)
		modes[i] = fModes[connector][i].mode;
	return B_OK;
}

status_t
RDNA4FrontendManager::GetPreferredMode(uint32 connector, display_mode* mode) const
{
	if (mode == NULL || connector >= 8 || fFrontend == NULL)
		return B_BAD_VALUE;
	if (connector >= fFrontend->Info().connector_count || fModeCounts[connector] == 0)
		return B_ENTRY_NOT_FOUND;
	for (uint32 i = 0; i < fModeCounts[connector]; i++) {
		if (fModes[connector][i].preferred) {
			*mode = fModes[connector][i].mode;
			return B_OK;
		}
	}
	*mode = fModes[connector][0].mode;
	return B_OK;
}

status_t
RDNA4FrontendManager::GetCurrentMode(display_mode* mode) const
{
	if (mode == NULL)
		return B_BAD_VALUE;
	if (!fHasCurrentMode)
		return B_ENTRY_NOT_FOUND;
	*mode = fCurrentMode;
	return B_OK;
}

status_t
RDNA4FrontendManager::GetEDID(uint32 connector, void* buffer, size_t size,
	size_t* _actual)
{
	if (fFrontend == NULL)
		return B_NO_INIT;
	return fFrontend->GetEDID(connector, buffer, size, _actual);
}
