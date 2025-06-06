/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus, superstippi@gmx.de
 *		Andrew Bachmann
 *		Stefano Ceccherini, burton666@libero.it
 *		Alexandre Deckner, alex@zappotek.com
 *		Axel Dörfler, axeld@pinc-software.de
 *		Rene Gollent, rene@gollent.com
 *		Thomas Kurschel
 *		Rafael Romo
 *		John Scipione, jscipione@gmail.com
 */


#include "ScreenWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <SpaceLayoutItem.h>
#include <Spinner.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <InterfacePrivate.h>

#include "AlertWindow.h"
#include "Constants.h"
#include "RefreshWindow.h"
#include "MonitorView.h"
#include "ScreenSettings.h"
#include "Utility.h"

/* Note, this headers defines a *private* interface to the Radeon accelerant.
 * It's a solution that works with the current BeOS interface that Haiku
 * adopted.
 * However, it's not a nice and clean solution. Don't use this header in any
 * application if you can avoid it. No other driver is using this, or should
 * be using this.
 * It will be replaced as soon as we introduce an updated accelerant interface
 * which may even happen before R1 hits the streets.
 */
#include "multimon.h"	// the usual: DANGER WILL, ROBINSON!


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


const char* kBackgroundsSignature = "application/x-vnd.Haiku-Backgrounds";

// list of officially supported colour spaces
static const struct {
	color_space	space;
	int32		bits_per_pixel;
	const char*	label;
} kColorSpaces[] = {
	{ B_CMAP8, 8, B_TRANSLATE("8 bits/pixel, 256 colors") },
	{ B_RGB15, 15, B_TRANSLATE("15 bits/pixel, 32768 colors") },
	{ B_RGB16, 16, B_TRANSLATE("16 bits/pixel, 65536 colors") },
	{ B_RGB24, 24, B_TRANSLATE("24 bits/pixel, 16 Million colors") },
	{ B_RGB32, 32, B_TRANSLATE("32 bits/pixel, 16 Million colors") }
};
static const int32 kColorSpaceCount = B_COUNT_OF(kColorSpaces);

// list of standard refresh rates
static const int32 kRefreshRates[] = { 60, 70, 72, 75, 80, 85, 95, 100 };
static const int32 kRefreshRateCount = B_COUNT_OF(kRefreshRates);

// list of combine modes
static const struct {
	combine_mode	mode;
	const char		*name;
} kCombineModes[] = {
	{ kCombineDisable, B_TRANSLATE("disable") },
	{ kCombineHorizontally, B_TRANSLATE("horizontally") },
	{ kCombineVertically, B_TRANSLATE("vertically") }
};
static const int32 kCombineModeCount = B_COUNT_OF(kCombineModes);


static BString
tv_standard_to_string(uint32 mode)
{
	switch (mode) {
		case 0:		return "disabled";
		case 1:		return "NTSC";
		case 2:		return "NTSC Japan";
		case 3:		return "PAL BDGHI";
		case 4:		return "PAL M";
		case 5:		return "PAL N";
		case 6:		return "SECAM";
		case 101:	return "NTSC 443";
		case 102:	return "PAL 60";
		case 103:	return "PAL NC";
		default:
		{
			BString name;
			name << "??? (" << mode << ")";

			return name;
		}
	}
}


static void
resolution_to_string(screen_mode& mode, BString &string)
{
	string.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			mode.width, mode.height);
}


static void
refresh_rate_to_string(float refresh, BString &string,
	bool appendUnit = true, bool alwaysWithFraction = false)
{
	snprintf(string.LockBuffer(32), 32, "%.*g", refresh >= 100.0 ? 4 : 3,
		refresh);
	string.UnlockBuffer();

	if (appendUnit)
		string << " " << B_TRANSLATE("Hz");
}


static const char*
screen_errors(status_t status)
{
	switch (status) {
		case B_ENTRY_NOT_FOUND:
			return B_TRANSLATE("Unknown mode");
		// TODO: add more?

		default:
			return strerror(status);
	}
}


//	#pragma mark - ScreenWindow


ScreenWindow::ScreenWindow(ScreenSettings* settings)
	:
	BWindow(settings->WindowFrame(), B_TRANSLATE_SYSTEM_NAME("Screen"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS, B_ALL_WORKSPACES),
	fIsVesa(false),
	fBootWorkspaceApplied(false),
	fUserSelectedColorSpace(NULL),
	fOtherRefresh(NULL),
	fScreenMode(this),
	fUndoScreenMode(this),
	fModified(false)
{
	BScreen screen(this);

	accelerant_device_info info;
	if (screen.GetDeviceInfo(&info) == B_OK
		&& !strcasecmp(info.chipset, "VESA"))
		fIsVesa = true;

	_UpdateOriginal();
	_BuildSupportedColorSpaces();
	fActive = fSelected = fOriginal;

	fSettings = settings;

	// we need the "Current Workspace" first to get its height

	BPopUpMenu* popUpMenu = new BPopUpMenu(B_TRANSLATE("Current workspace"),
		true, true);
	fAllWorkspacesItem = new BMenuItem(B_TRANSLATE("All workspaces"),
		new BMessage(WORKSPACE_CHECK_MSG));
	popUpMenu->AddItem(fAllWorkspacesItem);
	BMenuItem *item = new BMenuItem(B_TRANSLATE("Current workspace"),
		new BMessage(WORKSPACE_CHECK_MSG));

	popUpMenu->AddItem(item);
	fAllWorkspacesItem->SetMarked(true);

	BMenuField* workspaceMenuField = new BMenuField("WorkspaceMenu", NULL,
		popUpMenu);
	workspaceMenuField->ResizeToPreferred();

	// box on the left with workspace count and monitor view

	fScreenBox = new BBox("screen box");
	BGroupView* groupView = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	fScreenBox->AddChild(groupView);
	fScreenBox->SetLabel("placeholder");
		// Needed for layouting, will be replaced with screen name/size
	groupView->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);

	fDeviceInfo = new BStringView("device info", "");
	fDeviceInfo->SetAlignment(B_ALIGN_CENTER);
	groupView->AddChild(fDeviceInfo);

	float scaling = std::max(1.0f, be_plain_font->Size() / 12.0f);
	fMonitorView = new MonitorView(BRect(0.0, 0.0, 80.0 * scaling,
			80.0 * scaling), "monitor", screen.Frame().IntegerWidth() + 1,
		screen.Frame().IntegerHeight() + 1);
	fMonitorView->SetToolTip(B_TRANSLATE("Set background" B_UTF8_ELLIPSIS));
	groupView->AddChild(fMonitorView);

	// brightness slider
	fBrightnessSlider = new BSlider("brightness", B_TRANSLATE("Brightness:"),
		NULL, 0, 255, B_HORIZONTAL);
	groupView->AddChild(fBrightnessSlider);

	if (screen.GetBrightness(&fOriginalBrightness) == B_OK) {
		fBrightnessSlider->SetModificationMessage(
			new BMessage(SLIDER_BRIGHTNESS_MSG));
		fBrightnessSlider->SetValue(fOriginalBrightness * 255);
	} else {
		// The driver does not support changing the brightness,
		// so hide the slider
		fBrightnessSlider->Hide();
		fOriginalBrightness = -1;
	}

	// box on the left below the screen box with workspaces

	BBox* workspacesBox = new BBox("workspaces box");
	workspacesBox->SetLabel(B_TRANSLATE("Workspaces"));

	BGroupLayout* workspacesLayout = new BGroupLayout(B_VERTICAL);
	workspacesLayout->SetInsets(B_USE_DEFAULT_SPACING,
		be_control_look->DefaultItemSpacing() * 2, B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING);
	workspacesBox->SetLayout(workspacesLayout);

	fColumnsControl = new BSpinner("columns", B_TRANSLATE("Columns:"),
		new BMessage(kMsgWorkspaceColumnsChanged));
	fColumnsControl->SetAlignment(B_ALIGN_RIGHT);
	fColumnsControl->SetRange(1, 32);

	fRowsControl = new BSpinner("rows", B_TRANSLATE("Rows:"),
		new BMessage(kMsgWorkspaceRowsChanged));
	fRowsControl->SetAlignment(B_ALIGN_RIGHT);
	fRowsControl->SetRange(1, 32);

	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);
	fColumnsControl->SetValue(columns);
	fRowsControl->SetValue(rows);

	workspacesBox->AddChild(BLayoutBuilder::Group<>()
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.AddGroup(B_HORIZONTAL, 0)
				.AddGlue()
				.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
					// columns
					.Add(fColumnsControl->CreateLabelLayoutItem(), 0, 0)
					.Add(fColumnsControl->CreateTextViewLayoutItem(), 1, 0)
					// rows
					.Add(fRowsControl->CreateLabelLayoutItem(), 0, 1)
					.Add(fRowsControl->CreateTextViewLayoutItem(), 1, 1)
					.End()
				.AddGlue()
				.End()
			.End()
		.View());

	// put workspaces slider in a vertical group with a half space above so
	// if hidden you won't see the extra space.
	BView* workspacesView = BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.AddStrut(B_USE_HALF_ITEM_SPACING)
		.Add(workspacesBox)
		.View();

	// box on the right with screen resolution, etc.

	BBox* controlsBox = new BBox("controls box");
	controlsBox->SetLabel(workspaceMenuField);
	BGroupView* outerControlsView = new BGroupView(B_VERTICAL);
	outerControlsView->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);
	controlsBox->AddChild(outerControlsView);

	menu_layout layout = B_ITEMS_IN_COLUMN;

	// There are modes in the list with the same resolution but different bpp or refresh rates.
	// We don't want to take these into account when computing the menu layout, so we need to
	// count how many entries we will really have in the menu.
	int fullModeCount = fScreenMode.CountModes();
	int modeCount = 0;
	int index = 0;
	uint16 maxWidth = 0;
	uint16 maxHeight = 0;
	uint16 previousWidth = 0;
	uint16 previousHeight = 0;
	for (int32 i = 0; i < fullModeCount; i++) {
		screen_mode mode = fScreenMode.ModeAt(i);

		if (mode.width == previousWidth && mode.height == previousHeight)
			continue;
		modeCount++;
		previousWidth = mode.width;
		previousHeight = mode.height;
		if (maxWidth < mode.width)
			maxWidth = mode.width;
		if (maxHeight < mode.height)
			maxHeight = mode.height;
	}

	if (modeCount > 16)
		layout = B_ITEMS_IN_MATRIX;

	fResolutionMenu = new BPopUpMenu("resolution", true, true, layout);

	// Compute the size we should allocate to each item in the menu
	BRect itemRect;
	if (layout == B_ITEMS_IN_MATRIX) {
		BFont menuFont;
		font_height fontHeight;

		fResolutionMenu->GetFont(&menuFont);
		menuFont.GetHeight(&fontHeight);
		itemRect.left = itemRect.top = 0;
		itemRect.bottom = fontHeight.ascent + fontHeight.descent + 4;
		itemRect.right = menuFont.StringWidth("99999x99999") + 16;
		rows = modeCount / 3 + 1;
	}

	index = 0;
	for (int32 i = 0; i < fullModeCount; i++) {
		screen_mode mode = fScreenMode.ModeAt(i);

		if (mode.width == previousWidth && mode.height == previousHeight)
			continue;

		previousWidth = mode.width;
		previousHeight = mode.height;

		BMessage* message = new BMessage(POP_RESOLUTION_MSG);
		message->AddInt32("width", mode.width);
		message->AddInt32("height", mode.height);

		BString name;
		name.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			mode.width, mode.height);

		if (layout == B_ITEMS_IN_COLUMN)
			fResolutionMenu->AddItem(new BMenuItem(name.String(), message));
		else {
			int y = index % rows;
			int x = index / rows;
			itemRect.OffsetTo(x * itemRect.Width(), y * itemRect.Height());
			fResolutionMenu->AddItem(new BMenuItem(name.String(), message), itemRect);
		}

		index++;
	}

	fMonitorView->SetMaxResolution(maxWidth, maxHeight);

	fResolutionField = new BMenuField("ResolutionMenu",
		B_TRANSLATE("Resolution:"), fResolutionMenu);
	fResolutionField->SetAlignment(B_ALIGN_RIGHT);

	fColorsMenu = new BPopUpMenu("colors", true, false);

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		BMessage* message = new BMessage(POP_COLORS_MSG);
		message->AddInt32("bits_per_pixel", kColorSpaces[i].bits_per_pixel);
		message->AddInt32("space", kColorSpaces[i].space);

		BMenuItem* item = new BMenuItem(kColorSpaces[i].label, message);
		if (kColorSpaces[i].space == screen.ColorSpace())
			fUserSelectedColorSpace = item;

		fColorsMenu->AddItem(item);
	}

	fColorsField = new BMenuField("ColorsMenu", B_TRANSLATE("Colors:"),
		fColorsMenu);
	fColorsField->SetAlignment(B_ALIGN_RIGHT);

	fRefreshMenu = new BPopUpMenu("refresh rate", true, true);

	float min, max;
	if (fScreenMode.GetRefreshLimits(fActive, min, max) != B_OK) {
		// if we couldn't obtain the refresh limits, reset to the default
		// range. Constraints from detected monitors will fine-tune this
		// later.
		min = kRefreshRates[0];
		max = kRefreshRates[kRefreshRateCount - 1];
	}

	if (min == max) {
		// This is a special case for drivers that only support a single
		// frequency, like the VESA driver
		BString name;
		refresh_rate_to_string(min, name);
		BMessage *message = new BMessage(POP_REFRESH_MSG);
		message->AddFloat("refresh", min);
		BMenuItem *item = new BMenuItem(name.String(), message);
		fRefreshMenu->AddItem(item);
		item->SetEnabled(false);
	} else {
		monitor_info info;
		if (fScreenMode.GetMonitorInfo(info) == B_OK) {
			min = max_c(info.min_vertical_frequency, min);
			max = min_c(info.max_vertical_frequency, max);
		}

		for (int32 i = 0; i < kRefreshRateCount; ++i) {
			if (kRefreshRates[i] < min || kRefreshRates[i] > max)
				continue;

			BString name;
			name << kRefreshRates[i] << " " << B_TRANSLATE("Hz");

			BMessage *message = new BMessage(POP_REFRESH_MSG);
			message->AddFloat("refresh", kRefreshRates[i]);

			fRefreshMenu->AddItem(new BMenuItem(name.String(), message));
		}

		fOtherRefresh = new BMenuItem(B_TRANSLATE("Other" B_UTF8_ELLIPSIS),
			new BMessage(POP_OTHER_REFRESH_MSG));
		fRefreshMenu->AddItem(fOtherRefresh);
	}

	fRefreshField = new BMenuField("RefreshMenu", B_TRANSLATE("Refresh rate:"),
		fRefreshMenu);
	fRefreshField->SetAlignment(B_ALIGN_RIGHT);

	if (_IsVesa())
		fRefreshField->Hide();

	// enlarged area for multi-monitor settings
	{
		bool dummy;
		uint32 dummy32;
		bool multiMonSupport;
		bool useLaptopPanelSupport;
		bool tvStandardSupport;

		multiMonSupport = TestMultiMonSupport(&screen) == B_OK;
		useLaptopPanelSupport = GetUseLaptopPanel(&screen, &dummy) == B_OK;
		tvStandardSupport = GetTVStandard(&screen, &dummy32) == B_OK;

		// even if there is no support, we still create all controls
		// to make sure we don't access NULL pointers later on

		fCombineMenu = new BPopUpMenu("CombineDisplays",
			true, true);

		for (int32 i = 0; i < kCombineModeCount; i++) {
			BMessage *message = new BMessage(POP_COMBINE_DISPLAYS_MSG);
			message->AddInt32("mode", kCombineModes[i].mode);

			fCombineMenu->AddItem(new BMenuItem(kCombineModes[i].name,
				message));
		}

		fCombineField = new BMenuField("CombineMenu",
			B_TRANSLATE("Combine displays:"), fCombineMenu);
		fCombineField->SetAlignment(B_ALIGN_RIGHT);

		if (!multiMonSupport)
			fCombineField->Hide();

		fSwapDisplaysMenu = new BPopUpMenu("SwapDisplays",
			true, true);

		// !order is important - we rely that boolean value == idx
		BMessage *message = new BMessage(POP_SWAP_DISPLAYS_MSG);
		message->AddBool("swap", false);
		fSwapDisplaysMenu->AddItem(new BMenuItem(B_TRANSLATE("no"), message));

		message = new BMessage(POP_SWAP_DISPLAYS_MSG);
		message->AddBool("swap", true);
		fSwapDisplaysMenu->AddItem(new BMenuItem(B_TRANSLATE("yes"), message));

		fSwapDisplaysField = new BMenuField("SwapMenu",
			B_TRANSLATE("Swap displays:"), fSwapDisplaysMenu);
		fSwapDisplaysField->SetAlignment(B_ALIGN_RIGHT);

		if (!multiMonSupport)
			fSwapDisplaysField->Hide();

		fUseLaptopPanelMenu = new BPopUpMenu("UseLaptopPanel",
			true, true);

		// !order is important - we rely that boolean value == idx
		message = new BMessage(POP_USE_LAPTOP_PANEL_MSG);
		message->AddBool("use", false);
		fUseLaptopPanelMenu->AddItem(new BMenuItem(B_TRANSLATE("if needed"),
			message));

		message = new BMessage(POP_USE_LAPTOP_PANEL_MSG);
		message->AddBool("use", true);
		fUseLaptopPanelMenu->AddItem(new BMenuItem(B_TRANSLATE("always"),
			message));

		fUseLaptopPanelField = new BMenuField("UseLaptopPanel",
			B_TRANSLATE("Use laptop panel:"), fUseLaptopPanelMenu);
		fUseLaptopPanelField->SetAlignment(B_ALIGN_RIGHT);

		if (!useLaptopPanelSupport)
			fUseLaptopPanelField->Hide();

		fTVStandardMenu = new BPopUpMenu("TVStandard", true, true);

		// arbitrary limit
		uint32 i;
		for (i = 0; i < 100; ++i) {
			uint32 mode;
			if (GetNthSupportedTVStandard(&screen, i, &mode) != B_OK)
				break;

			BString name = tv_standard_to_string(mode);

			message = new BMessage(POP_TV_STANDARD_MSG);
			message->AddInt32("tv_standard", mode);

			fTVStandardMenu->AddItem(new BMenuItem(name.String(), message));
		}

		fTVStandardField = new BMenuField("tv standard",
			B_TRANSLATE("Video format:"), fTVStandardMenu);
		fTVStandardField->SetAlignment(B_ALIGN_RIGHT);

		if (!tvStandardSupport || i == 0)
			fTVStandardField->Hide();
	}

	BLayoutBuilder::Group<>(outerControlsView)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(fResolutionField->CreateLabelLayoutItem(), 0, 0)
			.Add(fResolutionField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(fColorsField->CreateLabelLayoutItem(), 0, 1)
			.Add(fColorsField->CreateMenuBarLayoutItem(), 1, 1)
			.Add(fRefreshField->CreateLabelLayoutItem(), 0, 2)
			.Add(fRefreshField->CreateMenuBarLayoutItem(), 1, 2)
			.Add(fCombineField->CreateLabelLayoutItem(), 0, 3)
			.Add(fCombineField->CreateMenuBarLayoutItem(), 1, 3)
			.Add(fSwapDisplaysField->CreateLabelLayoutItem(), 0, 4)
			.Add(fSwapDisplaysField->CreateMenuBarLayoutItem(), 1, 4)
			.Add(fUseLaptopPanelField->CreateLabelLayoutItem(), 0, 5)
			.Add(fUseLaptopPanelField->CreateMenuBarLayoutItem(), 1, 5)
			.Add(fTVStandardField->CreateLabelLayoutItem(), 0, 6)
			.Add(fTVStandardField->CreateMenuBarLayoutItem(), 1, 6)
		.End();

	// TODO: we don't support getting the screen's preferred settings
	/* fDefaultsButton = new BButton(buttonRect, "DefaultsButton", "Defaults",
		new BMessage(BUTTON_DEFAULTS_MSG));*/

	fApplyButton = new BButton("ApplyButton", B_TRANSLATE("Apply"),
		new BMessage(BUTTON_APPLY_MSG));
	fApplyButton->SetEnabled(false);
	BLayoutBuilder::Group<>(outerControlsView)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApplyButton);

	fRevertButton = new BButton("RevertButton", B_TRANSLATE("Revert"),
		new BMessage(BUTTON_REVERT_MSG));
	fRevertButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL)
			.AddGroup(B_VERTICAL, 0, 1)
				.AddStrut(floorf(controlsBox->TopBorderOffset()
					- fScreenBox->TopBorderOffset()))
				.Add(fScreenBox)
				.Add(workspacesView)
				.End()
			.AddGroup(B_VERTICAL, 0, 1)
				.Add(controlsBox, 2)
				.End()
			.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fRevertButton)
			.AddGlue()
			.End()
		.SetInsets(B_USE_WINDOW_SPACING);

	_UpdateControls();
	_UpdateMonitor();

	MoveOnScreen();
}


ScreenWindow::~ScreenWindow()
{
	delete fSettings;
}


bool
ScreenWindow::QuitRequested()
{
	fSettings->SetWindowFrame(Frame());

	// Write mode of workspace 0 (the boot workspace) to the vesa settings file
	screen_mode vesaMode;
	if (fBootWorkspaceApplied && fScreenMode.Get(vesaMode, 0) == B_OK) {
		status_t status = _WriteVesaModeFile(vesaMode);
		if (status < B_OK) {
			BString warning = B_TRANSLATE("Could not write VESA mode settings"
				" file:\n\t");
			warning << strerror(status);
			BAlert* alert = new BAlert(B_TRANSLATE("Warning"),
				warning.String(), B_TRANSLATE("OK"), NULL,
				NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}

	be_app->PostMessage(B_QUIT_REQUESTED);

	return BWindow::QuitRequested();
}


/*!	Update resolution list according to combine mode
	(some resolutions may not be combinable due to memory restrictions).
*/
void
ScreenWindow::_CheckResolutionMenu()
{
	for (int32 i = 0; i < fResolutionMenu->CountItems(); i++)
		fResolutionMenu->ItemAt(i)->SetEnabled(false);

	for (int32 i = 0; i < fScreenMode.CountModes(); i++) {
		screen_mode mode = fScreenMode.ModeAt(i);
		if (mode.combine != fSelected.combine)
			continue;

		BString name;
		name.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			mode.width, mode.height);

		BMenuItem *item = fResolutionMenu->FindItem(name.String());
		if (item != NULL)
			item->SetEnabled(true);
	}
}


/*!	Update color and refresh options according to current mode
	(a color space is made active if there is any mode with
	given resolution and this colour space; same applies for
	refresh rate, though "Other…" is always possible)
*/
void
ScreenWindow::_CheckColorMenu()
{
	int32 supportsAnything = false;
	int32 index = 0;

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		bool supported = false;

		for (int32 j = 0; j < fScreenMode.CountModes(); j++) {
			screen_mode mode = fScreenMode.ModeAt(j);

			if (fSelected.width == mode.width
				&& fSelected.height == mode.height
				&& kColorSpaces[i].space == mode.space
				&& fSelected.combine == mode.combine) {
				supportsAnything = true;
				supported = true;
				break;
			}
		}

		BMenuItem* item = fColorsMenu->ItemAt(index++);
		if (item)
			item->SetEnabled(supported);
	}

	fColorsField->SetEnabled(supportsAnything);

	if (!supportsAnything)
		return;

	// Make sure a valid item is selected

	BMenuItem* item = fColorsMenu->FindMarked();
	bool changed = false;

	if (item != fUserSelectedColorSpace) {
		if (fUserSelectedColorSpace != NULL
			&& fUserSelectedColorSpace->IsEnabled()) {
			fUserSelectedColorSpace->SetMarked(true);
			item = fUserSelectedColorSpace;
			changed = true;
		}
	}
	if (item != NULL && !item->IsEnabled()) {
		// find the next best item
		int32 index = fColorsMenu->IndexOf(item);
		bool found = false;

		for (int32 i = index + 1; i < fColorsMenu->CountItems(); i++) {
			item = fColorsMenu->ItemAt(i);
			if (item->IsEnabled()) {
				found = true;
				break;
			}
		}
		if (!found) {
			// search backwards as well
			for (int32 i = index - 1; i >= 0; i--) {
				item = fColorsMenu->ItemAt(i);
				if (item->IsEnabled())
					break;
			}
		}

		item->SetMarked(true);
		changed = true;
	}

	if (changed) {
		// Update selected space

		BMessage* message = item->Message();
		int32 space;
		if (message->FindInt32("space", &space) == B_OK) {
			fSelected.space = (color_space)space;
			_UpdateColorLabel();
		}
	}
}


/*!	Enable/disable refresh options according to current mode. */
void
ScreenWindow::_CheckRefreshMenu()
{
	float min, max;
	if (fScreenMode.GetRefreshLimits(fSelected, min, max) != B_OK || min == max)
		return;

	for (int32 i = fRefreshMenu->CountItems(); i-- > 0;) {
		BMenuItem* item = fRefreshMenu->ItemAt(i);
		BMessage* message = item->Message();
		float refresh;
		if (message != NULL && message->FindFloat("refresh", &refresh) == B_OK)
			item->SetEnabled(refresh >= min && refresh <= max);
	}
}


/*!	Activate appropriate menu item according to selected refresh rate */
void
ScreenWindow::_UpdateRefreshControl()
{
	if (isnan(fSelected.refresh)) {
		fRefreshMenu->SetEnabled(false);
		fOtherRefresh->SetLabel(B_TRANSLATE("Unknown"));
		fOtherRefresh->SetMarked(true);
		return;
	} else {
		fRefreshMenu->SetEnabled(true);
	}

	for (int32 i = 0; i < fRefreshMenu->CountItems(); i++) {
		BMenuItem* item = fRefreshMenu->ItemAt(i);
		if (item->Message()->FindFloat("refresh") == fSelected.refresh) {
			item->SetMarked(true);
			// "Other" items only contains a refresh rate when active
			if (fOtherRefresh != NULL)
				fOtherRefresh->SetLabel(B_TRANSLATE("Other" B_UTF8_ELLIPSIS));
			return;
		}
	}
	
	// this is a non-standard refresh rate
	if (fOtherRefresh != NULL) {
		fOtherRefresh->Message()->ReplaceFloat("refresh", fSelected.refresh);
		fOtherRefresh->SetMarked(true);

		BString string;
		refresh_rate_to_string(fSelected.refresh, string);
		fRefreshMenu->Superitem()->SetLabel(string.String());

		string.Append(B_TRANSLATE("/other" B_UTF8_ELLIPSIS));
		fOtherRefresh->SetLabel(string.String());
	}
}


void
ScreenWindow::_UpdateMonitorView()
{
	BMessage updateMessage(UPDATE_DESKTOP_MSG);
	updateMessage.AddInt32("width", fSelected.width);
	updateMessage.AddInt32("height", fSelected.height);

	PostMessage(&updateMessage, fMonitorView);
}


void
ScreenWindow::_UpdateControls()
{
	_UpdateWorkspaceButtons();

	BMenuItem* item = fSwapDisplaysMenu->ItemAt((int32)fSelected.swap_displays);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	item = fUseLaptopPanelMenu->ItemAt((int32)fSelected.use_laptop_panel);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	for (int32 i = 0; i < fTVStandardMenu->CountItems(); i++) {
		item = fTVStandardMenu->ItemAt(i);

		uint32 tvStandard;
		item->Message()->FindInt32("tv_standard", (int32 *)&tvStandard);
		if (tvStandard == fSelected.tv_standard) {
			if (!item->IsMarked())
				item->SetMarked(true);
			break;
		}
	}

	_CheckResolutionMenu();
	_CheckColorMenu();
	_CheckRefreshMenu();

	BString string;
	resolution_to_string(fSelected, string);
	item = fResolutionMenu->FindItem(string.String());

	if (item != NULL) {
		if (!item->IsMarked())
			item->SetMarked(true);
	} else {
		// this is bad luck - if mode has been set via screen references,
		// this case cannot occur; there are three possible solutions:
		// 1. add a new resolution to list
		//    - we had to remove it as soon as a "valid" one is selected
		//    - we don't know which frequencies/bit depths are supported
		//    - as long as we haven't the GMT formula to create
		//      parameters for any resolution given, we cannot
		//      really set current mode - it's just not in the list
		// 2. choose nearest resolution
		//    - probably a good idea, but implies coding and testing
		// 3. choose lowest resolution
		//    - do you really think we are so lazy? yes, we are
		item = fResolutionMenu->ItemAt(0);
		if (item)
			item->SetMarked(true);

		// okay - at least we set menu label to active resolution
		fResolutionMenu->Superitem()->SetLabel(string.String());
	}

	// mark active combine mode
	for (int32 i = 0; i < kCombineModeCount; i++) {
		if (kCombineModes[i].mode == fSelected.combine) {
			item = fCombineMenu->ItemAt(i);
			if (item != NULL && !item->IsMarked())
				item->SetMarked(true);
			break;
		}
	}

	item = fColorsMenu->ItemAt(0);

	for (int32 i = 0, index = 0; i <  kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		if (kColorSpaces[i].space == fSelected.space) {
			item = fColorsMenu->ItemAt(index);
			break;
		}

		index++;
	}

	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	_UpdateColorLabel();
	_UpdateMonitorView();
	_UpdateRefreshControl();

	_CheckApplyEnabled();
}


/*! Reflect active mode in chosen settings */
void
ScreenWindow::_UpdateActiveMode()
{
	_UpdateActiveMode(current_workspace());
}


void
ScreenWindow::_UpdateActiveMode(int32 workspace)
{
	// Usually, this function gets called after a mode
	// has been set manually; still, as the graphics driver
	// is free to fiddle with mode passed, we better ask
	// what kind of mode we actually got
	if (fScreenMode.Get(fActive, workspace) == B_OK) {
		fSelected = fActive;

		_UpdateMonitor();
		_BuildSupportedColorSpaces();
		_UpdateControls();
	}
}


void
ScreenWindow::_UpdateWorkspaceButtons()
{
	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	// Set the max values enabling/disabling the up/down arrows

	if (rows == 1)
		fColumnsControl->SetMaxValue(32);
	else if (rows == 2)
		fColumnsControl->SetMaxValue(16);
	else if (rows <= 4)
		fColumnsControl->SetMaxValue(8);
	else if (rows <= 8)
		fColumnsControl->SetMaxValue(4);
	else if (rows <= 16)
		fColumnsControl->SetMaxValue(2);
	else if (rows <= 32)
		fColumnsControl->SetMaxValue(1);

	if (columns == 1)
		fRowsControl->SetMaxValue(32);
	else if (columns == 2)
		fRowsControl->SetMaxValue(16);
	else if (columns <= 4)
		fRowsControl->SetMaxValue(8);
	else if (columns <= 8)
		fRowsControl->SetMaxValue(4);
	else if (columns <= 16)
		fRowsControl->SetMaxValue(2);
	else if (columns <= 32)
		fRowsControl->SetMaxValue(1);
}


void
ScreenWindow::ScreenChanged(BRect frame, color_space mode)
{
	// move window on screen, if necessary
	if (frame.right <= Frame().right
		&& frame.bottom <= Frame().bottom) {
		MoveTo((frame.Width() - Frame().Width()) / 2,
			(frame.Height() - Frame().Height()) / 2);
	}
}


void
ScreenWindow::WorkspaceActivated(int32 workspace, bool state)
{
	if (fScreenMode.GetOriginalMode(fOriginal, workspace) == B_OK) {
		_UpdateActiveMode(workspace);

		BMessage message(UPDATE_DESKTOP_COLOR_MSG);
		PostMessage(&message, fMonitorView);
	}
}


void
ScreenWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case WORKSPACE_CHECK_MSG:
			_CheckApplyEnabled();
			break;

		case kMsgWorkspaceColumnsChanged:
		{
			uint32 newColumns = (uint32)fColumnsControl->Value();

			uint32 rows;
			BPrivate::get_workspaces_layout(NULL, &rows);
			BPrivate::set_workspaces_layout(newColumns, rows);

			_UpdateWorkspaceButtons();
			fRowsControl->SetValue(rows);
				// enables/disables up/down arrows
			_CheckApplyEnabled();

			break;
		}

		case kMsgWorkspaceRowsChanged:
		{
			uint32 newRows = (uint32)fRowsControl->Value();

			uint32 columns;
			BPrivate::get_workspaces_layout(&columns, NULL);
			BPrivate::set_workspaces_layout(columns, newRows);

			_UpdateWorkspaceButtons();
			fColumnsControl->SetValue(columns);
				// enables/disables up/down arrows
			_CheckApplyEnabled();
			break;
		}

		case POP_RESOLUTION_MSG:
		{
			message->FindInt32("width", &fSelected.width);
			message->FindInt32("height", &fSelected.height);

			_CheckColorMenu();
			_CheckRefreshMenu();

			_UpdateMonitorView();
			_UpdateRefreshControl();

			_CheckApplyEnabled();
			break;
		}

		case POP_COLORS_MSG:
		{
			int32 space;
			if (message->FindInt32("space", &space) != B_OK)
				break;

			int32 index;
			if (message->FindInt32("index", &index) == B_OK
				&& fColorsMenu->ItemAt(index) != NULL)
				fUserSelectedColorSpace = fColorsMenu->ItemAt(index);

			fSelected.space = (color_space)space;
			_UpdateColorLabel();

			_CheckApplyEnabled();
			break;
		}

		case POP_REFRESH_MSG:
		{
			message->FindFloat("refresh", &fSelected.refresh);
			fOtherRefresh->SetLabel(B_TRANSLATE("Other" B_UTF8_ELLIPSIS));
				// revert "Other…" label - it might have a refresh rate prefix

			_CheckApplyEnabled();
			break;
		}

		case POP_OTHER_REFRESH_MSG:
		{
			// make sure menu shows something useful
			_UpdateRefreshControl();

			float min = 0, max = 999;
			fScreenMode.GetRefreshLimits(fSelected, min, max);
			if (min < gMinRefresh)
				min = gMinRefresh;
			if (max > gMaxRefresh)
				max = gMaxRefresh;

			monitor_info info;
			if (fScreenMode.GetMonitorInfo(info) == B_OK) {
				min = max_c(info.min_vertical_frequency, min);
				max = min_c(info.max_vertical_frequency, max);
			}

			RefreshWindow *fRefreshWindow = new RefreshWindow(
				fRefreshField->ConvertToScreen(B_ORIGIN), fSelected.refresh,
				min, max);
			fRefreshWindow->Show();
			break;
		}

		case SET_CUSTOM_REFRESH_MSG:
		{
			// user pressed "done" in "Other…" refresh dialog;
			// select the refresh rate chosen
			message->FindFloat("refresh", &fSelected.refresh);

			_UpdateRefreshControl();
			_CheckApplyEnabled();
			break;
		}

		case POP_COMBINE_DISPLAYS_MSG:
		{
			// new combine mode has bee chosen
			int32 mode;
			if (message->FindInt32("mode", &mode) == B_OK)
				fSelected.combine = (combine_mode)mode;

			_CheckResolutionMenu();
			_CheckApplyEnabled();
			break;
		}

		case POP_SWAP_DISPLAYS_MSG:
			message->FindBool("swap", &fSelected.swap_displays);
			_CheckApplyEnabled();
			break;

		case POP_USE_LAPTOP_PANEL_MSG:
			message->FindBool("use", &fSelected.use_laptop_panel);
			_CheckApplyEnabled();
			break;

		case POP_TV_STANDARD_MSG:
			message->FindInt32("tv_standard", (int32 *)&fSelected.tv_standard);
			_CheckApplyEnabled();
			break;

		case BUTTON_LAUNCH_BACKGROUNDS_MSG:
			if (be_roster->Launch(kBackgroundsSignature) == B_ALREADY_RUNNING) {
				app_info info;
				be_roster->GetAppInfo(kBackgroundsSignature, &info);
				be_roster->ActivateApp(info.team);
			}
			break;

		case BUTTON_DEFAULTS_MSG:
		{
			// TODO: get preferred settings of screen
			fSelected.width = 640;
			fSelected.height = 480;
			fSelected.space = B_CMAP8;
			fSelected.refresh = 60.0;
			fSelected.combine = kCombineDisable;
			fSelected.swap_displays = false;
			fSelected.use_laptop_panel = false;
			fSelected.tv_standard = 0;

			// TODO: workspace defaults

			_UpdateControls();
			break;
		}

		case BUTTON_UNDO_MSG:
			fUndoScreenMode.Revert();
			_UpdateActiveMode();
			break;

		case BUTTON_REVERT_MSG:
		{
			fModified = false;
			fBootWorkspaceApplied = false;

			// ScreenMode::Revert() assumes that we first set the correct
			// number of workspaces

			BPrivate::set_workspaces_layout(fOriginalWorkspacesColumns,
				fOriginalWorkspacesRows);
			_UpdateWorkspaceButtons();

			fScreenMode.Revert();

			BScreen screen(this);
			screen.SetBrightness(fOriginalBrightness);
			fBrightnessSlider->SetValue(fOriginalBrightness * 255);

			_UpdateActiveMode();
			break;
		}

		case BUTTON_APPLY_MSG:
			_Apply();
			break;

		case MAKE_INITIAL_MSG:
			// user pressed "keep" in confirmation dialog
			fModified = true;
			_UpdateActiveMode();
			break;

		case UPDATE_DESKTOP_COLOR_MSG:
			PostMessage(message, fMonitorView);
			break;

		case SLIDER_BRIGHTNESS_MSG:
		{
			BScreen screen(this);
			screen.SetBrightness(message->FindInt32("be:value") / 255.f);
			_CheckApplyEnabled();
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


status_t
ScreenWindow::_WriteVesaModeFile(const screen_mode& mode) const
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status < B_OK)
		return status;

	path.Append("kernel/drivers");
	status = create_directory(path.Path(), 0755);
	if (status < B_OK)
		return status;

	path.Append("vesa");
	BFile file;
	status = file.SetTo(path.Path(), B_CREATE_FILE | B_WRITE_ONLY | B_ERASE_FILE);
	if (status < B_OK)
		return status;

	char buffer[256];
	snprintf(buffer, sizeof(buffer), "mode %" B_PRId32 " %" B_PRId32 " %"
		B_PRId32 "\n", mode.width, mode.height, mode.BitsPerPixel());

	ssize_t bytesWritten = file.Write(buffer, strlen(buffer));
	if (bytesWritten < B_OK)
		return bytesWritten;

	return B_OK;
}


void
ScreenWindow::_BuildSupportedColorSpaces()
{
	fSupportedColorSpaces = 0;

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		for (int32 j = 0; j < fScreenMode.CountModes(); j++) {
			if (fScreenMode.ModeAt(j).space == kColorSpaces[i].space) {
				fSupportedColorSpaces |= 1 << i;
				break;
			}
		}
	}
}


void
ScreenWindow::_CheckApplyEnabled()
{
	bool applyEnabled = true;

	if (fSelected == fActive) {
		applyEnabled = false;
		if (fAllWorkspacesItem->IsMarked()) {
			screen_mode screenMode;
			const int32 workspaceCount = count_workspaces();
			for (int32 i = 0; i < workspaceCount; i++) {
				fScreenMode.Get(screenMode, i);
				if (screenMode != fSelected) {
					applyEnabled = true;
					break;
				}
			}
		}
	}

	fApplyButton->SetEnabled(applyEnabled);

	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	BScreen screen(this);
	float brightness = -1;
	screen.GetBrightness(&brightness);

	fRevertButton->SetEnabled(columns != fOriginalWorkspacesColumns
		|| rows != fOriginalWorkspacesRows
		|| brightness != fOriginalBrightness
		|| fSelected != fOriginal);
}


void
ScreenWindow::_UpdateOriginal()
{
	BPrivate::get_workspaces_layout(&fOriginalWorkspacesColumns,
		&fOriginalWorkspacesRows);

	fScreenMode.Get(fOriginal);
	fScreenMode.UpdateOriginalModes();
}


void
ScreenWindow::_UpdateMonitor()
{
	monitor_info info;
	float diagonalInches;
	status_t status = fScreenMode.GetMonitorInfo(info, &diagonalInches);
	if (status == B_OK) {
		char text[512];
		snprintf(text, sizeof(text), "%s%s%s %g\"", info.vendor,
			info.name[0] ? " " : "", info.name, diagonalInches);

		fScreenBox->SetLabel(text);
	} else {
		fScreenBox->SetLabel(B_TRANSLATE("Display info"));
	}

	// Add info about the graphics device

	accelerant_device_info deviceInfo;

	if (fScreenMode.GetDeviceInfo(deviceInfo) == B_OK) {
		BString deviceString;

		if (deviceInfo.name[0] && deviceInfo.chipset[0]) {
			deviceString.SetToFormat("%s (%s)", deviceInfo.name,
				deviceInfo.chipset);
		} else if (deviceInfo.name[0] || deviceInfo.chipset[0]) {
			deviceString
				= deviceInfo.name[0] ? deviceInfo.name : deviceInfo.chipset;
		}

		fDeviceInfo->SetText(deviceString);
	}


	char text[512];
	size_t length = 0;
	text[0] = 0;

	if (status == B_OK) {
		if (info.min_horizontal_frequency != 0
			&& info.min_vertical_frequency != 0
			&& info.max_pixel_clock != 0) {
			length = snprintf(text, sizeof(text),
				B_TRANSLATE("Horizonal frequency:\t%lu - %lu kHz\n"
					"Vertical frequency:\t%lu - %lu Hz\n\n"
					"Maximum pixel clock:\t%g MHz"),
				(long unsigned)info.min_horizontal_frequency,
				(long unsigned)info.max_horizontal_frequency,
				(long unsigned)info.min_vertical_frequency,
				(long unsigned)info.max_vertical_frequency,
				info.max_pixel_clock / 1000.0);
		}
		if (info.serial_number[0] && length < sizeof(text)) {
			if (length > 0) {
				text[length++] = '\n';
				text[length++] = '\n';
				text[length] = '\0';
			}
			length += snprintf(text + length, sizeof(text) - length,
				B_TRANSLATE("Serial no.: %s"), info.serial_number);
			if (info.produced.week != 0 && info.produced.year != 0
				&& length < sizeof(text)) {
				length += snprintf(text + length, sizeof(text) - length,
					" (%u/%u)", info.produced.week, info.produced.year);
			}
		}
	}

	if (text[0])
		fMonitorView->SetToolTip(text);
}


void
ScreenWindow::_UpdateColorLabel()
{
	BString string;
	string << fSelected.BitsPerPixel() << " " << B_TRANSLATE("bits/pixel");
	fColorsMenu->Superitem()->SetLabel(string.String());
}


void
ScreenWindow::_Apply()
{
	// make checkpoint, so we can undo these changes
	fUndoScreenMode.UpdateOriginalModes();

	status_t status = fScreenMode.Set(fSelected);
	if (status == B_OK) {
		// use the mode that has eventually been set and
		// thus we know to be working; it can differ from
		// the mode selected by user due to hardware limitation
		display_mode newMode;
		BScreen screen(this);
		screen.GetMode(&newMode);

		if (fAllWorkspacesItem->IsMarked()) {
			int32 originatingWorkspace = current_workspace();
			const int32 workspaceCount = count_workspaces();
			for (int32 i = 0; i < workspaceCount; i++) {
				if (i != originatingWorkspace)
					screen.SetMode(i, &newMode, true);
			}
			fBootWorkspaceApplied = true;
		} else {
			if (current_workspace() == 0)
				fBootWorkspaceApplied = true;
		}

		fActive = fSelected;

		// TODO: only show alert when this is an unknown mode
		BAlert* window = new AlertWindow(this);
		window->Go(NULL);
	} else {
		char message[256];
		snprintf(message, sizeof(message),
			B_TRANSLATE("The screen mode could not be set:\n\t%s\n"),
			screen_errors(status));
		BAlert* alert = new BAlert(B_TRANSLATE("Warning"), message,
			B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}
}
