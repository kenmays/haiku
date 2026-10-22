/*
 * Copyright 2005-2013, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "VirtualScreen.h"

#include "HWInterface.h"
#include "Desktop.h"

#include <new>


VirtualScreen::VirtualScreen()
	:
	fScreenList(4),
	fDrawingEngine(NULL),
	fHWInterface(NULL)
{
}


VirtualScreen::~VirtualScreen()
{
	_Reset();
}


void
VirtualScreen::_Reset()
{
	ScreenList list;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		list.AddItem(item->screen);
	}

	gScreenManager->ReleaseScreens(list);
	fScreenList.MakeEmpty();

	while (fScene.CountOutputs() > 0) {
		const DisplayOutput* output = fScene.OutputAt(0);
		if (output == NULL || fScene.RemoveOutput(output->id) != B_OK)
			break;
	}

	fFrame.Set(0, 0, 0, 0);
	fDrawingEngine = NULL;
	fHWInterface = NULL;
}


status_t
VirtualScreen::SetConfiguration(Desktop& desktop, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations, uint32* _changedScreens)
{
	// Remember previous screen modes

	typedef std::map<Screen*, display_mode> ScreenModeMap;
	ScreenModeMap previousModes;
	bool previousModesFailed = false;

	if (_changedScreens != NULL) {
		*_changedScreens = 0;

		try {
			for (int32 i = 0; i < fScreenList.CountItems(); i++) {
				Screen* screen = fScreenList.ItemAt(i)->screen;

				display_mode mode;
				screen->GetMode(mode);

				previousModes.insert(std::make_pair(screen, mode));
			}
		} catch (...) {
			previousModesFailed = true;
			*_changedScreens = ~0L;
		}
	}

	_Reset();

	ScreenList list;
	status_t status = gScreenManager->AcquireScreens(&desktop, NULL, 0,
		desktop.TargetScreen(), false, list);
	if (status != B_OK) {
		// TODO: we would try again here with force == true
		return status;
	}

	for (int32 i = 0; i < list.CountItems(); i++) {
		Screen* screen = list.ItemAt(i);

		AddScreen(screen, configurations, currentConfigurations);

		if (!previousModesFailed && _changedScreens != NULL) {
			// Figure out which screens have changed their mode
			display_mode mode;
			screen->GetMode(mode);

			ScreenModeMap::const_iterator found = previousModes.find(screen);
			if (found != previousModes.end()
				&& memcmp(&mode, &found->second, sizeof(display_mode)))
				*_changedScreens |= 1 << i;
		}
	}

	UpdateFrame();
	return B_OK;
}


status_t
VirtualScreen::AddScreen(Screen* screen, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations)
{
	screen_item* item = new(std::nothrow) screen_item;
	if (item == NULL)
		return B_NO_MEMORY;

	item->screen = screen;

	status_t status = B_ERROR;
	display_mode mode;
	if (_GetMode(screen, configurations, mode) == B_OK) {
		// we found settings for this screen, and try to apply them now
		status = screen->SetMode(mode);
	}

	if (status != B_OK) {
		// We found no configuration or it wasn't valid, try to fallback to
		// sane values
		status = screen->SetPreferredMode();
		if (status == B_OK) {
			screen->GetMode(mode);
		}
		if (status != B_OK)
			status = screen->SetBestMode(1024, 768, B_RGB32, 60.f);
		if (status != B_OK)
			status = screen->SetBestMode(800, 600, B_RGB32, 60.f, false);
		if (status != B_OK) {
			debug_printf("app_server: Failed to set mode: %s\n",
				strerror(status));

			delete item;
			return status;
		}
	}

	// SetMode() fallbacks may have succeeded without updating the local mode.
	// Always record the actual active mode in the saved configuration.
	screen->GetMode(mode);

	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;
	currentConfigurations.Set(screen->ID(), hasInfo ? &info : NULL, screen->Frame(), mode);

	// Keep the existing drawing backend as the compatibility path, while
	// recording each screen's logical placement independently.
	fDrawingEngine = screen->GetDrawingEngine();
	fHWInterface = screen->HWInterface();

	BRect screenFrame = screen->Frame();
	if (fScene.CountOutputs() > 0) {
		BRect bounds = fScene.Bounds();
		screenFrame.OffsetTo(bounds.right + 1, bounds.top);
	}
	item->frame = screenFrame;

	status = fScene.AddOutput(screen->ID(), item->frame);
	if (status != B_OK) {
		delete item;
		return status;
	}

	if (!fScreenList.AddItem(item)) {
		fScene.RemoveOutput(screen->ID());
		delete item;
		return B_NO_MEMORY;
	}

	UpdateFrame();
	return B_OK;
}


status_t
VirtualScreen::RemoveScreen(Screen* screen)
{
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen != screen)
			continue;

		status_t status = fScene.RemoveOutput(screen->ID());
		if (status != B_OK)
			return status;

		ScreenList releasedScreens;
		if (releasedScreens.AddItem(screen))
			gScreenManager->ReleaseScreens(releasedScreens);

		item = fScreenList.RemoveItem(i);
		delete item;

		if (fScreenList.CountItems() == 0) {
			fDrawingEngine = NULL;
			fHWInterface = NULL;
		} else {
			screen_item* remaining = fScreenList.ItemAt(0);
			fDrawingEngine = remaining->screen->GetDrawingEngine();
			fHWInterface = remaining->screen->HWInterface();
		}

		UpdateFrame();
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


void
VirtualScreen::UpdateFrame()
{
	if (fScene.CountOutputs() == 0) {
		fFrame.Set(0, 0, 0, 0);
		return;
	}

	fFrame = fScene.Bounds();
}


void
VirtualScreen::SetScreenFrame(int32 index, BRect frame)
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item == NULL || !frame.IsValid())
		return;

	status_t status = fScene.SetOutputFrame(item->screen->ID(), frame);
	if (status != B_OK)
		return;

	item->frame = frame;
	UpdateFrame();
}


/*!	Returns the smallest frame that spans over all screens
*/
BRect
VirtualScreen::Frame() const
{
	return fFrame;
}


Screen*
VirtualScreen::ScreenAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen;

	return NULL;
}


Screen*
VirtualScreen::ScreenByID(int32 id) const
{
	for (int32 i = fScreenList.CountItems(); i-- > 0;) {
		screen_item* item = fScreenList.ItemAt(i);

		if (item->screen->ID() == id || id == B_MAIN_SCREEN_ID.id)
			return item->screen;
	}

	return NULL;
}


Screen*
VirtualScreen::ScreenAtPoint(BPoint point) const
{
	int32 outputID = fScene.OutputAtPoint(point);
	if (outputID < 0)
		return NULL;

	return ScreenByID(outputID);
}


BRect
VirtualScreen::ScreenFrameAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->frame;

	return BRect(0, 0, 0, 0);
}


int32
VirtualScreen::CountScreens() const
{
	return fScreenList.CountItems();
}


status_t
VirtualScreen::_GetMode(Screen* screen, const ScreenConfigurations& configurations,
	display_mode& mode) const
{
	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;

	screen_configuration* configuration = configurations.BestFit(screen->ID(),
		hasInfo ? &info : NULL);
	if (configuration == NULL)
		return B_NAME_NOT_FOUND;

	mode = configuration->mode;
	return B_OK;
}
