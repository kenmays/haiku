#include "rdna4_display.h"

RDNA4Display::RDNA4Display()
	: fInitialized(false)
{
}

status_t
RDNA4Display::Initialize()
{
	/* DCN4 resource discovery and timing programming will be enabled only
	 * after the exact DCN4 register table is selected. */
	fInitialized = true;
	return B_OK;
}

status_t
RDNA4Display::GetModeList(display_mode** modes, uint32* count)
{
	if (modes == NULL || count == NULL)
		return B_BAD_VALUE;
	*modes = NULL;
	*count = 0;
	return B_NOT_SUPPORTED;
}

status_t
RDNA4Display::SetMode(const display_mode& mode)
{
	(void)mode;
	if (!fInitialized)
		return B_NO_INIT;
	return B_NOT_SUPPORTED;
}

status_t
RDNA4Display::SetCursor(const uint32* data, uint32 width, uint32 height)
{
	(void)data; (void)width; (void)height;
	return B_NOT_SUPPORTED;
}

status_t
RDNA4Display::MoveCursor(int32 x, int32 y)
{
	(void)x; (void)y;
	return B_NOT_SUPPORTED;
}

status_t
RDNA4Display::ShowCursor(bool visible)
{
	(void)visible;
	return B_NOT_SUPPORTED;
}
