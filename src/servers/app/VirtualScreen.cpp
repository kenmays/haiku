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
#include <math.h>


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
	// Release ownership one screen at a time without allocating a temporary
	// list. Cleanup must still work when memory pressure caused the reset.
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		gScreenManager->ReleaseScreen(item->screen);
	}

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

	status_t firstAddError = B_OK;
	for (int32 i = 0; i < list.CountItems(); i++) {
		Screen* screen = list.ItemAt(i);

		status_t addStatus = AddScreen(screen, configurations,
			currentConfigurations);
		if (addStatus != B_OK) {
			if (firstAddError == B_OK)
				firstAddError = addStatus;

			gScreenManager->ReleaseScreen(screen);
			continue;
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
	if (fScreenList.CountItems() == 0)
		return firstAddError != B_OK ? firstAddError : B_ERROR;

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

	// Keep the first screen as the deterministic legacy rendering backend.
	// The current Desktop and Window paths still expose a single DrawingEngine;
	// replacing it on every AddScreen() would silently route all drawing to
	// whichever output happened to be enumerated last. Per-output rendering
	// must be introduced at the call sites, not by changing this fallback.
	if (fScreenList.CountItems() == 0) {
		fDrawingEngine = screen->GetDrawingEngine();
		fHWInterface = screen->HWInterface();
	}

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

		gScreenManager->ReleaseScreen(screen);

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


status_t
VirtualScreen::SetScreenScale(int32 index, float scale)
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item == NULL)
		return B_BAD_INDEX;

	return fScene.SetOutputScale(item->screen->ID(), scale);
}


float
VirtualScreen::ScreenScaleAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item == NULL)
		return 1.0f;

	const DisplayOutput* output = fScene.OutputByID(item->screen->ID());
	return output != NULL ? output->scale : 1.0f;
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


Screen*
VirtualScreen::ScreenForFrame(BRect frame) const
{
	int32 outputID = fScene.OutputWithLargestIntersection(frame);
	return outputID >= 0 ? ScreenByID(outputID) : NULL;
}

status_t
VirtualScreen::GlobalToScreenPixel(const Screen* screen, BPoint global,
	BPoint& pixel) const
{
	if (screen == NULL || ScreenByID(screen->ID()) != screen)
		return B_BAD_VALUE;

	return fScene.GlobalToOutputPixel(screen->ID(), global, pixel);
}


status_t
VirtualScreen::ScreenPixelToGlobal(const Screen* screen, BPoint pixel,
	BPoint& global) const
{
	if (screen == NULL || ScreenByID(screen->ID()) != screen)
		return B_BAD_VALUE;

	return fScene.OutputPixelToGlobal(screen->ID(), pixel, global);
}


status_t
VirtualScreen::ClipRegionToScreen(const Screen* screen,
	const BRegion& globalRegion, BRegion& screenRegion) const
{
	if (screen == NULL)
		return B_BAD_VALUE;

	const screen_item* item = NULL;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* candidate = fScreenList.ItemAt(i);
		if (candidate != NULL && candidate->screen == screen) {
			item = candidate;
			break;
		}
	}
	if (item == NULL)
		return B_BAD_VALUE;

	const DisplayOutput* output = fScene.OutputByID(screen->ID());
	if (output == NULL || !output->enabled)
		return B_ENTRY_NOT_FOUND;
	if (output->scale != 1.0f)
		return B_NOT_SUPPORTED;

	// BRegion stores integer pixel coordinates. Refuse fractional origins
	// rather than silently clipping or shifting damage by a pixel.
	BRect frame = item->frame;
	if (!frame.IsValid() || frame.left != floorf(frame.left)
		|| frame.top != floorf(frame.top))
		return B_BAD_VALUE;

	BRegion clipped(globalRegion);
	BRegion outputBounds(frame);
	clipped.IntersectWith(&outputBounds);
	clipped.OffsetBy(-((int32)frame.left), -((int32)frame.top));
	screenRegion = clipped;
	return B_OK;
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
