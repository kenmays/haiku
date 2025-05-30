/*
 * Copyright 2006 - 2011, Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "MainWindow.h"

#include <stdio.h>

#include <Directory.h>
#include <File.h>
#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <GroupLayout.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>

#include "support.h"

#include "App.h"
#include "LaunchButton.h"
#include "NamePanel.h"
#include "PadView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LaunchBox"
MainWindow::MainWindow(const char* name, BRect frame, bool addDefaultButtons)
	:
	BWindow(frame, name, B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
		| B_WILL_ACCEPT_FIRST_CLICK | B_NO_WORKSPACE_ACTIVATION
		| B_AUTO_UPDATE_SIZE_LIMITS | B_SAME_POSITION_IN_ALL_WORKSPACES,
		B_ALL_WORKSPACES),
	fSettings(new BMessage('sett')),
	fPadView(new PadView("pad view")),
	fAutoRaise(false),
	fShowOnAllWorkspaces(true)
{
	bool buttonsAdded = false;
	if (load_settings(fSettings, "main_settings", "LaunchBox") >= B_OK)
		buttonsAdded = LoadSettings(fSettings);
	if (!buttonsAdded) {
		if (addDefaultButtons)
			_AddDefaultButtons();
		else
			_AddEmptyButtons();
	}
	SetLayout(new BGroupLayout(B_HORIZONTAL));
	AddChild(fPadView);
}


MainWindow::MainWindow(const char* name, BRect frame, BMessage* settings)
	:
	BWindow(frame, name,
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
		| B_WILL_ACCEPT_FIRST_CLICK | B_NO_WORKSPACE_ACTIVATION
		| B_AUTO_UPDATE_SIZE_LIMITS | B_SAME_POSITION_IN_ALL_WORKSPACES,
		B_ALL_WORKSPACES),
	fSettings(settings),
	fPadView(new PadView("pad view")),
	fAutoRaise(false),
	fShowOnAllWorkspaces(true)
{
	if (!LoadSettings(settings))
		_AddEmptyButtons();

	SetLayout(new BGroupLayout(B_HORIZONTAL));
	AddChild(fPadView);
}


MainWindow::~MainWindow()
{
	delete fSettings;
}


bool
MainWindow::QuitRequested()
{
	int32 padWindowCount = 0;
	for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++) {
		if (dynamic_cast<MainWindow*>(window))
			padWindowCount++;
	}
	bool canClose = true;
	
	if (padWindowCount == 1) {
		be_app->PostMessage(B_QUIT_REQUESTED);
		canClose = false;
	} else {
		BAlert* alert = new BAlert(B_TRANSLATE("last chance"),
			B_TRANSLATE("Really close this pad?\n"
							"(The pad will not be remembered.)"),
			B_TRANSLATE("Close"), B_TRANSLATE("Cancel"), NULL);
		alert->SetShortcut(1, B_ESCAPE);
		if (alert->Go() == 1)
			canClose = false;
	}
	return canClose;
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_LAUNCH: 
		{
			BView* pointer;
			if (message->FindPointer("be:source", (void**)&pointer) < B_OK)
				break;
			LaunchButton* button = dynamic_cast<LaunchButton*>(pointer);
			if (button == NULL)
				break;
			BString errorMessage;
			bool launchedByRef = false;
			if (button->Ref()) {
				BEntry entry(button->Ref(), true);
				if (entry.IsDirectory()) {
					// open in Tracker
					BMessenger messenger("application/x-vnd.Be-TRAK");
					if (messenger.IsValid()) {
						BMessage trackerMessage(B_REFS_RECEIVED);
						trackerMessage.AddRef("refs", button->Ref());
						status_t ret = messenger.SendMessage(&trackerMessage);
						if (ret < B_OK) {
							errorMessage = B_TRANSLATE("Failed to send "
							"'open folder' command to Tracker.\n\nError: ");
							errorMessage << strerror(ret);
						} else
							launchedByRef = true;
					} else
						errorMessage = B_TRANSLATE("Failed to open folder - is Tracker running?");
				} else {
					status_t ret = be_roster->Launch(button->Ref());
					if (ret < B_OK && ret != B_ALREADY_RUNNING) {
						BString errStr(B_TRANSLATE("Failed to launch '%1'.\n"
							"\nError:"));
						BPath path(button->Ref());
						if (path.InitCheck() >= B_OK)
							errStr.ReplaceFirst("%1", path.Path());
						else
							errStr.ReplaceFirst("%1", button->Ref()->name);
						errorMessage << errStr.String() << " ";
						errorMessage << strerror(ret);
					} else
						launchedByRef = true;
				}
			}
			if (!launchedByRef && button->AppSignature()) {
				status_t ret = be_roster->Launch(button->AppSignature());
				if (ret != B_OK && ret != B_ALREADY_RUNNING) {
					BString errStr(B_TRANSLATE("\n\nFailed to launch application "
						"with signature '%2'.\n\nError:"));
					errStr.ReplaceFirst("%2", button->AppSignature());
					errorMessage << errStr.String() << " ";
					errorMessage << strerror(ret);
				} else {
					// clear error message on success (might have been
					// filled when trying to launch by ref)
					errorMessage = "";
				}
			} else if (!launchedByRef) {
				errorMessage = B_TRANSLATE("Failed to launch 'something', "
					"error in Pad data.");
			}
			if (errorMessage.Length() > 0) {
				BAlert* alert = new BAlert("error", errorMessage.String(),
					B_TRANSLATE("Bummer"), NULL, NULL, B_WIDTH_FROM_WIDEST);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go(NULL);
			}
			break;
		}
		case MSG_ADD_SLOT: 
		{
			LaunchButton* button;
			if (message->FindPointer("be:source", (void**)&button) >= B_OK) {
				fPadView->AddButton(new LaunchButton("launch button",
					NULL, new BMessage(MSG_LAUNCH)), button);
			}
			break;
		}
		case MSG_CLEAR_SLOT: 
		{
			LaunchButton* button;
			if (message->FindPointer("be:source", (void**)&button) >= B_OK)
				button->SetTo((entry_ref*)NULL);
			break;
		}
		case MSG_REMOVE_SLOT: 
		{
			LaunchButton* button;
			if (message->FindPointer("be:source", (void**)&button) >= B_OK) {
				if (fPadView->RemoveButton(button))
					delete button;
			}
			break;
		}
		case MSG_SET_DESCRIPTION: 
		{
			LaunchButton* button;
			if (message->FindPointer("be:source", (void**)&button) >= B_OK) {
				const char* name;
				if (message->FindString("name", &name) >= B_OK) {
					// message comes from a previous name panel
					button->SetDescription(name);
					BRect namePanelFrame;
					if (message->FindRect("frame", &namePanelFrame) == B_OK) {
						((App*)be_app)->SetNamePanelSize(
							namePanelFrame.Size());
					}
				} else {
					// message comes from pad view
					entry_ref* ref = button->Ref();
					if (ref) {
						BString helper(B_TRANSLATE("Description for '%3'"));
						helper.ReplaceFirst("%3", ref->name);
						// Place the name panel besides the pad, but give it
						// the user configured size.
						BPoint origin = B_ORIGIN;
						BSize size = ((App*)be_app)->NamePanelSize();
						NamePanel* panel = new NamePanel(helper.String(),
							button->Description(), this, this,
							new BMessage(*message), size);
						panel->Layout(true);
						size = panel->Frame().Size();
						BScreen screen(this);
						BPoint mousePos;
						uint32 buttons;
						fPadView->GetMouse(&mousePos, &buttons, false);
						fPadView->ConvertToScreen(&mousePos);
						if (fPadView->Orientation() == B_HORIZONTAL) {
							// Place above or below the pad
							origin.x = mousePos.x - size.width / 2;
							if (screen.Frame().bottom - Frame().bottom
									> size.height + 20) {
								origin.y = Frame().bottom + 10;
							} else {
								origin.y = Frame().top - 10 - size.height;
							}
						} else {
							// Place left or right of the pad
							origin.y = mousePos.y - size.height / 2;
							if (screen.Frame().right - Frame().right
									> size.width + 20) {
								origin.x = Frame().right + 10;
							} else {
								origin.x = Frame().left - 10 - size.width;
							}
						}
						panel->MoveTo(origin);
						panel->Show();
					}
				}
			}
			break;
		}
		case MSG_ADD_WINDOW: 
		{
			BMessage settings('sett');
			SaveSettings(&settings);
			message->AddMessage("window", &settings);
			be_app->PostMessage(message);
			break;
		}
		case MSG_SHOW_BORDER:
			SetLook(B_TITLED_WINDOW_LOOK);
			break;
		case MSG_HIDE_BORDER:
			SetLook(B_BORDERED_WINDOW_LOOK);
			break;
		case MSG_TOGGLE_AUTORAISE:
			ToggleAutoRaise();
			break;
		case MSG_SHOW_ON_ALL_WORKSPACES:
			fShowOnAllWorkspaces = !fShowOnAllWorkspaces;
			if (fShowOnAllWorkspaces)
				SetWorkspaces(B_ALL_WORKSPACES);
			else
				SetWorkspaces(1L << current_workspace());
			break;
		case MSG_OPEN_CONTAINING_FOLDER: 
		{
			LaunchButton* button;
			if (message->FindPointer("be:source", (void**)&button) == B_OK && button->Ref() != NULL) {
				entry_ref target = *button->Ref();
				BEntry openTarget(&target);
				BMessage openMsg(B_REFS_RECEIVED);
				BMessenger tracker("application/x-vnd.Be-TRAK");
				openTarget.GetParent(&openTarget);
				openTarget.GetRef(&target);
				openMsg.AddRef("refs",&target);
				tracker.SendMessage(&openMsg);
			}
		}
		break;
		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED:
		case B_PASTE:
		case B_MODIFIERS_CHANGED:
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
MainWindow::Show()
{
	BWindow::Show();
	_GetLocation();
}


void
MainWindow::ScreenChanged(BRect frame, color_space format)
{
	_AdjustLocation(Frame());
}


void
MainWindow::WorkspaceActivated(int32 workspace, bool active)
{
	if (fShowOnAllWorkspaces) {
		if (!active)
			_GetLocation();
		else
			_AdjustLocation(Frame());
	}
}


void
MainWindow::FrameMoved(BPoint origin)
{
	if (IsActive()) {
		_GetLocation();
		_NotifySettingsChanged();
	}
}


void
MainWindow::FrameResized(float width, float height)
{
	if (IsActive()) {
		_GetLocation();
		_NotifySettingsChanged();
	}
	BWindow::FrameResized(width, height);
}


void
MainWindow::ToggleAutoRaise()
{
	fAutoRaise = !fAutoRaise;
	if (fAutoRaise)
		fPadView->SetEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	else
		fPadView->SetEventMask(0);

	_NotifySettingsChanged();
}


bool
MainWindow::LoadSettings(const BMessage* message)
{
	// restore window positioning
	BPoint point;
	bool useAdjust = false;
	if (message->FindPoint("window position", &point) == B_OK) {
		fScreenPosition = point;
		useAdjust = true;
	}
	float borderDist;
	if (message->FindFloat("border distance", &borderDist) == B_OK) {
		fBorderDist = borderDist;
	}
	// restore window frame
	BRect frame;
	if (message->FindRect("window frame", &frame) == B_OK) {
		if (useAdjust) {
			_AdjustLocation(frame);
		} else {
			make_sure_frame_is_on_screen(frame, this);
			MoveTo(frame.LeftTop());
			ResizeTo(frame.Width(), frame.Height());
		}
	}

	// restore window look
	window_look look;
	if (message->FindInt32("window look", (int32*)&look) == B_OK)
		SetLook(look);

	// restore orientation
	int32 orientation;
	if (message->FindInt32("orientation", &orientation) == B_OK)
		fPadView->SetOrientation((enum orientation)orientation);

	// restore icon size
	int32 iconSize;
	if (message->FindInt32("icon size", &iconSize) == B_OK)
		fPadView->SetIconSize(iconSize);

	// restore ignore double click
	bool ignoreDoubleClick;
	if (message->FindBool("ignore double click", &ignoreDoubleClick) == B_OK)
		fPadView->SetIgnoreDoubleClick(ignoreDoubleClick);

	// restore buttons
	const char* path;
	bool buttonAdded = false;
	for (int32 i = 0; message->FindString("path", i, &path) >= B_OK; i++) {
		LaunchButton* button = new LaunchButton("launch button",
			NULL, new BMessage(MSG_LAUNCH));
		fPadView->AddButton(button);
		BString signature;
		if (message->FindString("signature", i, &signature) >= B_OK
			&& signature.CountChars() > 0)  {
			button->SetTo(signature.String(), true);
		}

		BEntry entry(path, true);
		entry_ref ref;
		if (entry.Exists() && entry.GetRef(&ref) == B_OK)
			button->SetTo(&ref);

		const char* text;
		if (message->FindString("description", i, &text) >= B_OK)
			button->SetDescription(text);
		buttonAdded = true;
	}

	// restore auto raise setting
	bool autoRaise;
	if (message->FindBool("auto raise", &autoRaise) == B_OK && autoRaise)
		ToggleAutoRaise();

	// store workspace setting
	bool showOnAllWorkspaces;
	if (message->FindBool("all workspaces", &showOnAllWorkspaces) == B_OK) {
		fShowOnAllWorkspaces = showOnAllWorkspaces;
		SetWorkspaces(showOnAllWorkspaces
			? B_ALL_WORKSPACES : 1L << current_workspace());
	}
	if (!fShowOnAllWorkspaces) {
		uint32 workspaces;
		if (message->FindInt32("workspaces", (int32*)&workspaces) == B_OK)
			SetWorkspaces(workspaces);
	}

	return buttonAdded;
}


void
MainWindow::SaveSettings(BMessage* message)
{
	// make sure the positioning info is correct
	_GetLocation();
	// store window position
	if (message->ReplacePoint("window position", fScreenPosition) != B_OK)
		message->AddPoint("window position", fScreenPosition);

	if (message->ReplaceFloat("border distance", fBorderDist) != B_OK)
		message->AddFloat("border distance", fBorderDist);

	// store window frame and look
	if (message->ReplaceRect("window frame", Frame()) != B_OK)
		message->AddRect("window frame", Frame());

	if (message->ReplaceInt32("window look", Look()) != B_OK)
		message->AddInt32("window look", Look());

	// store orientation
	if (message->ReplaceInt32("orientation",
			(int32)fPadView->Orientation()) != B_OK)
		message->AddInt32("orientation", (int32)fPadView->Orientation());

	// store icon size
	if (message->ReplaceInt32("icon size", fPadView->IconSize()) != B_OK)
		message->AddInt32("icon size", fPadView->IconSize());

	// store ignore double click
	if (message->ReplaceBool("ignore double click",
			fPadView->IgnoreDoubleClick()) != B_OK) {
		message->AddBool("ignore double click", fPadView->IgnoreDoubleClick());
	}

	// store buttons
	message->RemoveName("path");
	message->RemoveName("description");
	message->RemoveName("signature");
	for (int32 i = 0; LaunchButton* button = fPadView->ButtonAt(i); i++) {
		BPath path(button->Ref());
		if (path.InitCheck() >= B_OK)
			message->AddString("path", path.Path());
		else
			message->AddString("path", "");
		message->AddString("description", button->Description());

		if (button->AppSignature())
			message->AddString("signature", button->AppSignature());
		else
			message->AddString("signature", "");
	}

	// store auto raise setting
	if (message->ReplaceBool("auto raise", fAutoRaise) != B_OK)
		message->AddBool("auto raise", fAutoRaise);

	// store workspace setting
	if (message->ReplaceBool("all workspaces", fShowOnAllWorkspaces) != B_OK)
		message->AddBool("all workspaces", fShowOnAllWorkspaces);
	if (message->ReplaceInt32("workspaces", Workspaces()) != B_OK)
		message->AddInt32("workspaces", Workspaces());
}


void
MainWindow::_GetLocation()
{
	BRect frame = Frame();
	BPoint origin = frame.LeftTop();
	BPoint center(origin.x + frame.Width() / 2.0,
		origin.y + frame.Height() / 2.0);
	BScreen screen(this);
	BRect screenFrame = screen.Frame();
	fScreenPosition.x = center.x / screenFrame.Width();
	fScreenPosition.y = center.y / screenFrame.Height();
	if (fabs(0.5 - fScreenPosition.x) > fabs(0.5 - fScreenPosition.y)) {
		// nearest to left or right border
		if (fScreenPosition.x < 0.5)
			fBorderDist = frame.left - screenFrame.left;
		else
			fBorderDist = screenFrame.right - frame.right;
	} else {
		// nearest to top or bottom border
		if (fScreenPosition.y < 0.5)
			fBorderDist = frame.top - screenFrame.top;
		else
			fBorderDist = screenFrame.bottom - frame.bottom;
	}
}


void
MainWindow::_AdjustLocation(BRect frame)
{
	BScreen screen(this);
	BRect screenFrame = screen.Frame();
	BPoint center(fScreenPosition.x * screenFrame.Width(),
		fScreenPosition.y * screenFrame.Height());
	BPoint frameCenter(frame.left + frame.Width() / 2.0,
		frame.top + frame.Height() / 2.0);
	frame.OffsetBy(center - frameCenter);
	// ignore border dist when distance too large
	if (fBorderDist < 10.0) {
		// see which border we mean depending on screen position
		BPoint offset(0.0, 0.0);
		if (fabs(0.5 - fScreenPosition.x) > fabs(0.5 - fScreenPosition.y)) {
			// left or right border
			if (fScreenPosition.x < 0.5)
				offset.x = (screenFrame.left + fBorderDist) - frame.left;
			else
				offset.x = (screenFrame.right - fBorderDist) - frame.right;
		} else {
			// top or bottom border
			if (fScreenPosition.y < 0.5)
				offset.y = (screenFrame.top + fBorderDist) - frame.top;
			else
				offset.y = (screenFrame.bottom - fBorderDist) - frame.bottom;
		}
		frame.OffsetBy(offset);
	}

	make_sure_frame_is_on_screen(frame, this);

	MoveTo(frame.LeftTop());
	ResizeTo(frame.Width(), frame.Height());
}


void
MainWindow::_AddDefaultButtons()
{
	// Mail
	LaunchButton* button = new LaunchButton("launch button", NULL,
		new BMessage(MSG_LAUNCH));
	fPadView->AddButton(button);
	button->SetTo("application/x-vnd.Be-MAIL", true);

	// StyledEdit
	button = new LaunchButton("launch button", NULL, new BMessage(MSG_LAUNCH));
	fPadView->AddButton(button);
	button->SetTo("application/x-vnd.Haiku-StyledEdit", true);

	// ShowImage
	button = new LaunchButton("launch button", NULL, new BMessage(MSG_LAUNCH));
	fPadView->AddButton(button);
	button->SetTo("application/x-vnd.Haiku-ShowImage", true);

	// MediaPlayer
	button = new LaunchButton("launch button", NULL, new BMessage(MSG_LAUNCH));
	fPadView->AddButton(button);
	button->SetTo("application/x-vnd.Haiku-MediaPlayer", true);

	// DeskCalc
	button = new LaunchButton("launch button", NULL, new BMessage(MSG_LAUNCH));
	fPadView->AddButton(button);
	button->SetTo("application/x-vnd.Haiku-DeskCalc", true);

	// Terminal
	button = new LaunchButton("launch button", NULL, new BMessage(MSG_LAUNCH));
	fPadView->AddButton(button);
	button->SetTo("application/x-vnd.Haiku-Terminal", true);
}


void
MainWindow::_AddEmptyButtons()
{
	LaunchButton* button = new LaunchButton("launch button", NULL,
		new BMessage(MSG_LAUNCH));
	fPadView->AddButton(button);

	button = new LaunchButton("launch button", NULL, new BMessage(MSG_LAUNCH));
	fPadView->AddButton(button);

	button = new LaunchButton("launch button", NULL, new BMessage(MSG_LAUNCH));
	fPadView->AddButton(button);
}


void
MainWindow::_NotifySettingsChanged()
{
	be_app->PostMessage(MSG_SETTINGS_CHANGED);
}
