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

		status = AddScreen(screen, configurations, currentConfigurations);
		if (status != B_OK) {
			// Do not leave screens acquired when one screen cannot be
			// configured. Drop any partially built virtual-screen state
			// and release the complete acquisition list, including screens
			// that were not added to fScreenList.
			_Reset();
			gScreenManager->ReleaseScreens(list);
			return status;
		}

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

	// Use the actual active mode, including when the fallback mode setter
	// succeeded without updating the local mode variable.
	screen->GetMode(mode);
	BRect originalFrame = screen->Frame();
	item->frame = originalFrame;

	// Restore the saved desktop origin when a matching configuration exists.
	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;
	screen_configuration* configuration = configurations.BestFit(
		screen->ID(), hasInfo ? &info : NULL);
	bool hasSavedFrame = configuration != NULL
		&& configuration->frame.IsValid();
	if (hasSavedFrame)
		item->frame.OffsetTo(configuration->frame.LeftTop());

	// Place screens without saved positions to the right of the current
	// virtual desktop. Explicit saved positions are preserved.
	if (!hasSavedFrame && fScreenList.CountItems() > 0) {
		float right = fFrame.right;
		for (int32 i = 0; i < fScreenList.CountItems(); i++) {
			screen_item* existing = fScreenList.ItemAt(i);
			right = max_c(right, existing->frame.right);
		}
		item->frame.OffsetTo(right + 1, 0);
	}

	// Keep the Screen object's origin and the virtual-screen layout in sync.
	screen->SetFrame(item->frame);

	if (!fScreenList.AddItem(item)) {
		delete item;
		screen->SetFrame(originalFrame);
		UpdateFrame();
		return B_NO_MEMORY;
	}

	float brightness = configuration != NULL ? configuration->brightness : 1.0f;
	status = currentConfigurations.Set(screen->ID(), hasInfo ? &info : NULL,
		item->frame, mode, brightness);
	if (status != B_OK) {
		// The list owns screen_item; remove it and restore the Screen's previous
		// desktop origin so a failed configuration save cannot leave partial state.
		fScreenList.RemoveItem(item);
		screen->SetFrame(originalFrame);
		UpdateFrame();
		return status;
	}

	// TODO: this works only for single screen configurations
	fDrawingEngine = screen->GetDrawingEngine();
	fHWInterface = screen->HWInterface();
	UpdateFrame();

	return B_OK;
}


status_t
VirtualScreen::RemoveScreen(Screen* screen)
{
	// not implemented yet (config changes when running)
	return B_ERROR;
}


void
VirtualScreen::UpdateFrame()
{
	bool hasFrame = false;
	BRect frame(0, 0, -1, -1);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item == NULL || item->screen == NULL)
			continue;

		// Rebuild dimensions from the current mode but preserve the assigned
		// desktop origin. This keeps geometry coherent after a mode change.
		item->frame = item->screen->Frame();
		if (!item->frame.IsValid())
			continue;

		if (!hasFrame) {
			frame = item->frame;
			hasFrame = true;
		} else {
			frame.left = min_c(frame.left, item->frame.left);
			frame.top = min_c(frame.top, item->frame.top);
			frame.right = max_c(frame.right, item->frame.right);
			frame.bottom = max_c(frame.bottom, item->frame.bottom);
		}
	}

	fFrame = hasFrame ? frame : BRect(0, 0, -1, -1);
}


void
VirtualScreen::SetScreenFrame(int32 index, BRect frame)
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item == NULL || !frame.IsValid())
		return;

	item->screen->SetFrame(frame);
	item->frame = item->screen->Frame();
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
	// The main screen is the first screen in the virtual-screen list.
	// Resolve it before searching explicit IDs; otherwise the main-screen
	// alias matches every item and the reverse loop returns the last screen.
	if (id == B_MAIN_SCREEN_ID.id) {
		screen_item* item = fScreenList.ItemAt(0);
		return item != NULL ? item->screen : NULL;
	}

	for (int32 i = fScreenList.CountItems(); i-- > 0;) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen->ID() == id)
			return item->screen;
	}

	return NULL;
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
