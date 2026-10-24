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
#include <cmath>
#include <algorithm>


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

	// Keep the first screen as the deterministic legacy fallback for code
	// paths that still request one DrawingEngine. Multi-output window drawing
	// explicitly binds each intersecting output and must not be redirected by
	// whichever connector happened to be enumerated last.
	if (fScreenList.CountItems() == 0) {
		fDrawingEngine = screen->GetDrawingEngine();
		fHWInterface = screen->HWInterface();
	}

	BRect screenFrame = screen->Frame();
	float screenScale = 1.0f;

	// Restore saved logical geometry, not only its origin. A saved frame whose
	// extent differs from the physical mode encodes the output's scale. Require
	// consistent horizontal/vertical ratios so stale or malformed settings do
	// not accidentally distort the desktop.
	screen_configuration* configuration = configurations.BestFit(
		screen->ID(), hasInfo ? &info : NULL);
	bool useSavedFrame = configuration != NULL
		&& configuration->frame.IsValid();
	if (useSavedFrame) {
		const BRect& savedFrame = configuration->frame;
		if (savedFrame.Width() > 0.0f && savedFrame.Height() > 0.0f
			&& mode.virtual_width > 1 && mode.virtual_height > 1) {
			float scaleX = (mode.virtual_width - 1.0f) / savedFrame.Width();
			float scaleY = (mode.virtual_height - 1.0f) / savedFrame.Height();
			float tolerance = std::max(0.01f,
				std::max(fabsf(scaleX), fabsf(scaleY)) * 0.01f);
			if (std::isfinite(scaleX) && std::isfinite(scaleY)
				&& scaleX > 0.0f && scaleY > 0.0f
				&& fabsf(scaleX - scaleY) <= tolerance) {
				screenScale = (scaleX + scaleY) * 0.5f;
				screenFrame.right = screenFrame.left
					+ (mode.virtual_width - 1.0f) / screenScale;
				screenFrame.bottom = screenFrame.top
					+ (mode.virtual_height - 1.0f) / screenScale;
			}
		}

		screenFrame.OffsetTo(savedFrame.LeftTop());
		if (fScene.OutputWithLargestIntersection(screenFrame) >= 0)
			useSavedFrame = false;
	}

	if (!useSavedFrame) {
		screenScale = 1.0f;
		screenFrame = screen->Frame();
		if (fScene.CountOutputs() > 0) {
			BRect bounds = fScene.Bounds();
			screenFrame.OffsetTo(bounds.right + 1, bounds.top);
		}
	}
	item->frame = screenFrame;

	status = fScene.AddOutput(screen->ID(), item->frame, screenScale);
	if (status != B_OK) {
		delete item;
		return status;
	}

	if (!fScreenList.AddItem(item)) {
		fScene.RemoveOutput(screen->ID());
		delete item;
		return B_NO_MEMORY;
	}


	// Save the actual geometry selected above, not Screen::Frame()'s
	// origin-based framebuffer bounds. This makes the desktop layout survive
	// the next app_server configuration cycle.
	currentConfigurations.Set(screen->ID(), hasInfo ? &info : NULL,
		item->frame, mode);

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

		// ScreenManager owns the removal transaction and will release this
		// screen after the owner has detached it. Calling ReleaseScreen() here
		// would re-enter ScreenManager's lock during a hot-unplug callback.
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

	const DisplayOutput* output = fScene.OutputByID(item->screen->ID());
	if (output == NULL)
		return B_ENTRY_NOT_FOUND;

	float previousScale = output->scale;
	BRect previousFrame = item->frame;

	status_t status = fScene.SetOutputScale(item->screen->ID(), scale);
	if (status != B_OK)
		return status;

	// A scale factor changes the logical size, not the framebuffer mode.
	// Keep the output origin fixed and derive the logical extent from the
	// active pixel dimensions.
	display_mode mode;
	item->screen->GetMode(mode);
	if (mode.virtual_width == 0 || mode.virtual_height == 0) {
		fScene.SetOutputScale(item->screen->ID(), previousScale);
		return B_BAD_VALUE;
	}

	BRect frame = previousFrame;
	frame.right = frame.left + (mode.virtual_width - 1) / scale;
	frame.bottom = frame.top + (mode.virtual_height - 1) / scale;
	if (!frame.IsValid()
		|| !std::isfinite(frame.left) || !std::isfinite(frame.top)
		|| !std::isfinite(frame.right) || !std::isfinite(frame.bottom)
		|| (double)frame.left < -2147483648.0
		|| (double)frame.left > 2147483647.0
		|| (double)frame.top < -2147483648.0
		|| (double)frame.top > 2147483647.0
		|| (double)frame.right < -2147483648.0
		|| (double)frame.right > 2147483647.0
		|| (double)frame.bottom < -2147483648.0
		|| (double)frame.bottom > 2147483647.0) {
		fScene.SetOutputScale(item->screen->ID(), previousScale);
		return B_BAD_VALUE;
	}

	status = fScene.SetOutputFrame(item->screen->ID(), frame);
	if (status != B_OK) {
		fScene.SetOutputScale(item->screen->ID(), previousScale);
		return status;
	}

	item->frame = frame;
	UpdateFrame();
	return B_OK;
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
	// The legacy main-screen token is an alias for the primary output, not
	// a wildcard. Check it before walking the list; combining it with the ID
	// comparison below made every output match and returned the last one.
	if (id == B_MAIN_SCREEN_ID.id) {
		screen_item* item = fScreenList.ItemAt(0);
		return item != NULL ? item->screen : NULL;
	}

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item != NULL && item->screen->ID() == id)
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
	screenRegion.MakeEmpty();
	if (screen == NULL)
		return B_BAD_VALUE;

	bool found = false;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* candidate = fScreenList.ItemAt(i);
		if (candidate != NULL && candidate->screen == screen) {
			found = true;
			break;
		}
	}
	if (!found)
		return B_BAD_VALUE;

	display_mode mode;
	screen->GetMode(mode);
	if (mode.virtual_width == 0 || mode.virtual_height == 0)
		return B_BAD_VALUE;

	return fScene.ClipRegionToOutput(screen->ID(), globalRegion,
		mode.virtual_width, mode.virtual_height, screenRegion);
}


status_t
VirtualScreen::BuildRenderTarget(const Screen* screen,
	const BRegion& globalDamage, OutputRenderTarget& target) const
{
	if (screen == NULL || globalDamage.CountRects() <= 0)
		return B_BAD_VALUE;

	bool found = false;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item == NULL || item->screen != screen)
			continue;
		found = true;
		break;
	}
	if (!found || screen->HWInterface() == NULL)
		return B_NAME_NOT_FOUND;

	const DisplayOutput* output = fScene.OutputByID(screen->ID());
	if (output == NULL || !output->enabled || !output->frame.IsValid()
		|| output->frame.left != floor(output->frame.left)
		|| output->frame.top != floor(output->frame.top)
		|| !std::isfinite(output->scale) || output->scale <= 0.0f) {
		return B_NAME_NOT_FOUND;
	}

	BRegion logicalClip(globalDamage);
	BRegion outputBounds(output->frame);
	logicalClip.IntersectWith(&outputBounds);
	if (logicalClip.CountRects() <= 0)
		return B_NAME_NOT_FOUND;

	double pixelLeft = (double)output->frame.left * output->scale;
	double pixelTop = (double)output->frame.top * output->scale;
	if (!std::isfinite(pixelLeft) || !std::isfinite(pixelTop))
		return B_BAD_VALUE;

	// The rasterizer's native translation is integer-only. Split the exact
	// output origin into an integer translation and a subpixel residual so
	// fractional-DPI origins remain representable without rounding the layout.
	double integerPixelLeft = floor(pixelLeft);
	double integerPixelTop = floor(pixelTop);
	if (integerPixelLeft < -2147483647.0
		|| integerPixelLeft > 2147483647.0
		|| integerPixelTop < -2147483647.0
		|| integerPixelTop > 2147483647.0) {
		return B_BAD_VALUE;
	}
	float fractionalOffsetX = (float)(integerPixelLeft - pixelLeft);
	float fractionalOffsetY = (float)(integerPixelTop - pixelTop);
	if (!std::isfinite(fractionalOffsetX)
		|| !std::isfinite(fractionalOffsetY)) {
		return B_BAD_VALUE;
	}

	display_mode mode;
	screen->GetMode(mode);
	if (mode.virtual_width == 0 || mode.virtual_height == 0)
		return B_BAD_VALUE;

	BRegion pixelClip;
	status_t status = fScene.ClipRegionToOutput(screen->ID(), logicalClip,
		mode.virtual_width, mode.virtual_height, pixelClip);
	if (status != B_OK)
		return status;
	if (pixelClip.CountRects() <= 0)
		return B_NAME_NOT_FOUND;

	target = OutputRenderTarget(screen->ID(), output->frame, output->scale,
		-(int32)integerPixelLeft, -(int32)integerPixelTop, logicalClip,
		pixelClip, fractionalOffsetX, fractionalOffsetY);
	target.generation = fScene.Generation();
	target.pixelWidth = mode.virtual_width;
	target.pixelHeight = mode.virtual_height;
	target.colorSpace = mode.space;
	return B_OK;
}


bool
VirtualScreen::IsRenderTargetCurrent(const OutputRenderTarget& target) const
{
	if (target.screenID < 0 || target.generation == 0
		|| target.generation != fScene.Generation()) {
		return false;
	}

	Screen* screen = ScreenByID(target.screenID);
	if (screen == NULL || screen->HWInterface() == NULL)
		return false;

	const DisplayOutput* output = fScene.OutputByID(target.screenID);
	if (output == NULL || !output->enabled
		|| output->frame.left != target.frame.left
		|| output->frame.top != target.frame.top
		|| output->frame.right != target.frame.right
		|| output->frame.bottom != target.frame.bottom
		|| output->scale != target.scale) {
		return false;
	}

	display_mode mode;
	screen->GetMode(mode);
	return mode.virtual_width == target.pixelWidth
		&& mode.virtual_height == target.pixelHeight
		&& mode.space == target.colorSpace
		&& target.pixelWidth > 0 && target.pixelHeight > 0;
}


status_t
VirtualScreen::BuildRenderPlan(const BRegion& globalDamage,
	std::vector<OutputRenderTarget>& targets) const
{
	targets.clear();
	if (globalDamage.CountRects() <= 0)
		return B_OK;

	try {
		for (int32 i = 0; i < fScreenList.CountItems(); i++) {
			screen_item* item = fScreenList.ItemAt(i);
			if (item == NULL || item->screen == NULL)
				continue;

			OutputRenderTarget target;
			status_t status = BuildRenderTarget(item->screen, globalDamage,
				target);
			if (status == B_NAME_NOT_FOUND)
				continue;
			if (status != B_OK) {
				// Do not return a seemingly complete plan after an allocation or
				// backend failure. Callers can choose a safe fallback, but must
				// never execute a partially constructed plan unknowingly.
				targets.clear();
				return status;
			}

			targets.push_back(target);
		}
	} catch (...) {
		targets.clear();
		return B_NO_MEMORY;
	}

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
