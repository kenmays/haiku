/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/


#include "DeskWindow.h"

#include <AppFileInfo.h>
#include <Catalog.h>
#include <Debug.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <Messenger.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PathFinder.h>
#include <PathMonitor.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Screen.h>
#include <String.h>
#include <StringList.h>
#include <Volume.h>
#include <WindowPrivate.h>

#include <fcntl.h>
#include <unistd.h>

#include "Attributes.h"
#include "AutoLock.h"
#include "BackgroundImage.h"
#include "Commands.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "KeyInfos.h"
#include "MountMenu.h"
#include "PoseView.h"
#include "Shortcuts.h"
#include "TemplatesMenu.h"
#include "Tracker.h"


const char* kShelfPath = "tracker_shelf";
	// replicant support

const char* kShortcutsSettings = "shortcuts_settings";
const char* kDefaultShortcut = "BEOS:default_shortcut";
const uint32 kDefaultModifiers = B_OPTION_KEY | B_COMMAND_KEY;


static struct AddOnInfo*
MatchOne(struct AddOnInfo* item, void* castToName)
{
	if (strcmp(item->model->Name(), (const char*)castToName) == 0) {
		// found match, bail out
		return item;
	}

	return 0;
}


static void
AddOneShortcut(Model* model, char key, uint32 modifiers, BDeskWindow* window)
{
	if (key == '\0')
		return;

	BMessage* runAddOn = new BMessage(kLoadAddOn);
	runAddOn->AddRef("refs", model->EntryRef());
	window->AddShortcut(key, modifiers, runAddOn);
}


static struct AddOnInfo*
RevertToDefault(struct AddOnInfo* item, void* castToWindow)
{
	if (item->key != item->defaultKey || item->modifiers != kDefaultModifiers) {
		BDeskWindow* window = static_cast<BDeskWindow*>(castToWindow);
		if (window != NULL) {
			window->RemoveShortcut(item->key, item->modifiers);
			item->key = item->defaultKey;
			item->modifiers = kDefaultModifiers;
			AddOneShortcut(item->model, item->key, item->modifiers, window);
		}
	}

	return 0;
}


static struct AddOnInfo*
FindElement(struct AddOnInfo* item, void* castToOther)
{
	Model* other = static_cast<Model*>(castToOther);
	if (*item->model->EntryRef() == *other->EntryRef())
		return item;

	return 0;
}


static void
LoadAddOnDir(BDirectory directory, BDeskWindow* window,
	LockingList<AddOnInfo, true>* list)
{
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		Model* model = new Model(&entry);
		if (model->InitCheck() == B_OK && model->IsSymLink()) {
			// resolve symlinks
			Model* resolved = new Model(model->EntryRef(), true, true);
			if (resolved->InitCheck() == B_OK)
				model->SetLinkTo(resolved);
			else
				delete resolved;
		}
		if (model->InitCheck() != B_OK
			|| !model->ResolveIfLink()->IsExecutable()) {
			delete model;
			continue;
		}

		char* name = strdup(model->Name());
		if (!list->EachElement(MatchOne, name)) {
			struct AddOnInfo* item = new struct AddOnInfo;
			item->model = model;

			item->has_populate_menu = B_NO_INIT;

			BResources resources(model->ResolveIfLink()->EntryRef());
			size_t size;
			char* shortcut = (char*)resources.LoadResource(B_STRING_TYPE,
				kDefaultShortcut, &size);
			if (shortcut == NULL || strlen(shortcut) > 1)
				item->key = '\0';
			else
				item->key = shortcut[0];
			AddOneShortcut(model, item->key, kDefaultModifiers, window);
			item->defaultKey = item->key;
			item->modifiers = kDefaultModifiers;
			list->AddItem(item);

			// load supported types (if any)
			BFile file(item->model->EntryRef(), B_READ_ONLY);
			if (file.InitCheck() == B_OK) {
				BAppFileInfo info(&file);
				if (info.InitCheck() == B_OK) {
					BMessage types;
					if (info.GetSupportedTypes(&types) == B_OK) {
						int32 i = 0;
						BString supportedType;
						while (types.FindString("types", i, &supportedType) == B_OK) {
							item->supportedTypes.Add(supportedType);
							i++;
						}
					}
				}
			}
		}
		free(name);
	}

	node_ref nodeRef;
	directory.GetNodeRef(&nodeRef);

	TTracker::WatchNode(&nodeRef, B_WATCH_DIRECTORY, window);
}


// #pragma mark - BDeskWindow


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DeskWindow"


BDeskWindow::BDeskWindow(LockingList<BWindow>* windowList, uint32 openFlags)
	:
	BContainerWindow(windowList, openFlags, kDesktopWindowLook, kDesktopWindowFeel,
		B_NOT_MOVABLE | B_WILL_ACCEPT_FIRST_CLICK | B_NOT_ZOOMABLE | B_NOT_CLOSABLE
			| B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES, false),
	fDeskShelf(NULL),
	fNodeRef(NULL),
	fShortcutsSettings(NULL)
{
	// create pose view
	BDirectory deskDir;
	if (FSGetDeskDir(&deskDir) == B_OK) {
		BEntry entry;
		deskDir.GetEntry(&entry);
		Model* model = new Model(&entry, true);
		if (model->InitCheck() == B_OK)
			CreatePoseView(model);
		else
			delete model;
	}
}


BDeskWindow::~BDeskWindow()
{
	SaveDesktopPoseLocations();
		// explicit call to SavePoseLocations so that extended pose info
		// gets committed properly
	PoseView()->DisableSaveLocation();
		// prevent double-saving, this would slow down quitting
	PoseView()->StopSettingsWatch();
	stop_watching(this);
}


void
BDeskWindow::Init(const BMessage*)
{
	// Set the size of the screen before calling the container window's
	// Init() because it will add volume poses to this window and
	// they will be clipped otherwise

	BScreen screen(this);
	fOldFrame = screen.Frame();

	ResizeTo(fOldFrame.Width(), fOldFrame.Height());

	InitKeyIndices();
	InitAddOnsList(false);
	ApplyShortcutPreferences(false);

	_inherited::Init();

	entry_ref ref;
	BPath path;
	if (!BootedInSafeMode() && FSFindTrackerSettingsDir(&path) == B_OK) {
		path.Append(kShelfPath);
		close(open(path.Path(), O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR
			| S_IRGRP | S_IROTH));
		if (get_ref_for_path(path.Path(), &ref) == B_OK)
			fDeskShelf = new BShelf(&ref, PoseView());

		if (fDeskShelf != NULL)
			fDeskShelf->SetDisplaysZombies(true);
	}

	// Add icon view switching shortcuts. These are displayed in the context
	// menu, although they obviously don't work from those menu items.
	BMessage* message = new BMessage(kIconMode);
	AddShortcut('1', B_COMMAND_KEY, message, PoseView());

	message = new BMessage(kMiniIconMode);
	AddShortcut('2', B_COMMAND_KEY, message, PoseView());

	message = new BMessage(kIconMode);
	message->AddInt32("scale", 1);
	AddShortcut('+', B_COMMAND_KEY, message, PoseView());

	message = new BMessage(kIconMode);
	message->AddInt32("scale", 0);
	AddShortcut('-', B_COMMAND_KEY, message, PoseView());

	if (TrackerSettings().ShowDisksIcon()) {
		// create model for root of everything
		BEntry entry("/");
		Model model(&entry);
		if (model.InitCheck() == B_OK) {
			// add the root icon to desktop window
			BMessage message;
			message.what = B_NODE_MONITOR;
			message.AddInt32("opcode", B_ENTRY_CREATED);
			message.AddInt32("device", model.NodeRef()->device);
			message.AddInt64("node", model.NodeRef()->node);
			message.AddInt64("directory", model.EntryRef()->directory);
			message.AddString("name", model.EntryRef()->name);

			PostMessage(&message, PoseView());
		}
	}
}


void
BDeskWindow::InitAddOnsList(bool update)
{
	AutoLock<LockingList<AddOnInfo, true> > lock(fAddOnsList);
	if (!lock.IsLocked())
		return;

	if (update) {
		for (int i = fAddOnsList->CountItems() - 1; i >= 0; i--) {
			AddOnInfo* item = fAddOnsList->ItemAt(i);
			RemoveShortcut(item->key, B_OPTION_KEY | B_COMMAND_KEY);
		}
		fAddOnsList->MakeEmpty(true);
	}

	BStringList addOnPaths;
	BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "Tracker",
		addOnPaths);
	int32 count = addOnPaths.CountStrings();
	for (int32 i = 0; i < count; i++)
		LoadAddOnDir(BDirectory(addOnPaths.StringAt(i)), this, fAddOnsList);

	BMessage message(kRebuildAddOnMenus);
	dynamic_cast<TTracker*>(be_app)->PostMessageToAllContainerWindows(message);
}


void
BDeskWindow::ApplyShortcutPreferences(bool update)
{
	AutoLock<LockingList<AddOnInfo, true> > lock(fAddOnsList);
	if (!lock.IsLocked())
		return;

	if (!update) {
		BPath path;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
			BPathMonitor::StartWatching(path.Path(),
				B_WATCH_STAT | B_WATCH_FILES_ONLY, this);
			path.Append(kShortcutsSettings);
			fShortcutsSettings = new char[strlen(path.Path()) + 1];
			strcpy(fShortcutsSettings, path.Path());
		}
	}

	fAddOnsList->EachElement(RevertToDefault, this);

	BFile shortcutSettings(fShortcutsSettings, B_READ_ONLY);
	BMessage fileMsg;
	if (shortcutSettings.InitCheck() != B_OK
		|| fileMsg.Unflatten(&shortcutSettings) != B_OK) {
		fNodeRef = NULL;
		return;
	}
	shortcutSettings.GetNodeRef(fNodeRef);

	int32 i = 0;
	BMessage message;
	while (fileMsg.FindMessage("spec", i++, &message) == B_OK) {
		int32 key;
		if (message.FindInt32("key", &key) != B_OK)
			continue;

		// only handle shortcuts referring add-ons
		BString command;
		if (message.FindString("command", &command) != B_OK)
			continue;

		bool isInAddOns = false;

		BStringList addOnPaths;
		BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY,
			"Tracker/", addOnPaths);
		for (int32 i = 0; i < addOnPaths.CountStrings(); i++) {
			if (command.StartsWith(addOnPaths.StringAt(i))) {
				isInAddOns = true;
				break;
			}
		}

		if (!isInAddOns)
			continue;

		BEntry entry(command);
		if (entry.InitCheck() != B_OK)
			continue;

		const char* shortcut = GetKeyName(key);
		if (strlen(shortcut) != 1)
			continue;

		uint32 modifiers = B_COMMAND_KEY;
			// it's required by interface kit to at least
			// have B_COMMAND_KEY
		int32 value;
		if (message.FindInt32("mcidx", 0, &value) == B_OK)
			modifiers |= (value != 0 ? B_SHIFT_KEY : 0);

		if (message.FindInt32("mcidx", 1, &value) == B_OK)
			modifiers |= (value != 0 ? B_CONTROL_KEY : 0);

		if (message.FindInt32("mcidx", 3, &value) == B_OK)
			modifiers |= (value != 0 ? B_OPTION_KEY : 0);

		Model model(&entry);
		AddOnInfo* item = fAddOnsList->EachElement(FindElement, &model);
		if (item != NULL) {
			if (item->key != '\0')
				RemoveShortcut(item->key, item->modifiers);

			item->key = shortcut[0];
			item->modifiers = modifiers;
			AddOneShortcut(&model, item->key, item->modifiers, this);
		}
	}

	message = BMessage(kRebuildAddOnMenus);
	dynamic_cast<TTracker*>(be_app)->PostMessageToAllContainerWindows(message);
}


void
BDeskWindow::Quit()
{
	if (fNavigationItem != NULL) {
		// this duplicates BContainerWindow::Quit because
		// fNavigationItem can be part of fTrashContextMenu
		// and would get deleted with it
		BMenu* menu = fNavigationItem->Menu();
		if (menu != NULL)
			menu->RemoveItem(fNavigationItem);

		delete fNavigationItem;
		fNavigationItem = NULL;
	}

	fAddOnsList->MakeEmpty(true);
	delete fAddOnsList;

	delete fDeskShelf;

	// inherited will clean up the rest
	_inherited::Quit();
}


BPoseView*
BDeskWindow::NewPoseView(Model* model, uint32 viewMode)
{
	return new DesktopPoseView(model, viewMode);
}


void
BDeskWindow::CreatePoseView(Model* model)
{
	fPoseView = NewPoseView(model, kIconMode);
	fPoseView->SetIconMapping(false);
	fPoseView->SetEnsurePosesVisible(true);
	fPoseView->SetAutoScroll(false);

	BScreen screen(this);
	rgb_color desktopColor = screen.DesktopColor();
	if (desktopColor.alpha != 255) {
		desktopColor.alpha = 255;
#if B_BEOS_VERSION > B_BEOS_VERSION_5
		// This call seems to have the power to cause R5 to freeze!
		// Please report if commenting this out helped or helped not
		// on your system
		screen.SetDesktopColor(desktopColor);
#endif
	}

	fPoseView->SetViewColor(desktopColor);
	fPoseView->SetLowColor(desktopColor);

	fPoseView->SetResizingMode(B_FOLLOW_ALL);
	fPoseView->ResizeTo(Bounds().Size());
	AddChild(fPoseView);

	PoseView()->StartSettingsWatch();
}


void
BDeskWindow::WorkspaceActivated(int32 workspace, bool state)
{
	if (fBackgroundImage)
		fBackgroundImage->WorkspaceActivated(PoseView(), workspace, state);
}


void
BDeskWindow::SaveDesktopPoseLocations()
{
	PoseView()->SavePoseLocations(&fOldFrame);
}


void
BDeskWindow::ScreenChanged(BRect frame, color_space space)
{
	bool frameChanged = (frame != fOldFrame);

	SaveDesktopPoseLocations();
	fOldFrame = frame;
	ResizeTo(frame.Width(), frame.Height());

	if (fBackgroundImage)
		fBackgroundImage->ScreenChanged(frame, space);

	PoseView()->CheckPoseVisibility(frameChanged ? &frame : 0);
		// if frame changed, pass new frame so that icons can
		// get rearranged based on old pose info for the frame
}


void
BDeskWindow::UpdateDesktopBackgroundImages()
{
	WindowStateNodeOpener opener(this, false);
	fBackgroundImage = BackgroundImage::Refresh(fBackgroundImage,
		opener.Node(), true, PoseView());
}


void
BDeskWindow::Show()
{
	if (fBackgroundImage)
		fBackgroundImage->Show(PoseView(), current_workspace());

	PoseView()->CheckPoseVisibility();

	_inherited::Show();
}


bool
BDeskWindow::ShouldAddScrollBars() const
{
	return false;
}


bool
BDeskWindow::ShouldAddMenus() const
{
	return false;
}


bool
BDeskWindow::ShouldAddContainerView() const
{
	return false;
}


void
BDeskWindow::MessageReceived(BMessage* message)
{
	if (message->WasDropped()) {
		const rgb_color* color;
		ssize_t size;
		// handle "roColour"-style color drops
		if (message->FindData("RGBColor", 'RGBC',
			(const void**)&color, &size) == B_OK) {
			BScreen(this).SetDesktopColor(*color);
			PoseView()->SetViewColor(*color);
			PoseView()->SetLowColor(*color);

			// Notify the backgrounds app that the background changed
			status_t initStatus;
			BMessenger messenger("application/x-vnd.Haiku-Backgrounds", -1,
				&initStatus);
			if (initStatus == B_OK)
				messenger.SendMessage(message);

			return;
		}
	}

	switch (message->what) {
		case B_PATH_MONITOR:
		{
			const char* path = "";
			if (!(message->FindString("path", &path) == B_OK
					&& strcmp(path, fShortcutsSettings) == 0)) {

				dev_t device;
				ino_t node;
				if (fNodeRef == NULL
					|| message->FindInt32("device", &device) != B_OK
					|| message->FindInt64("node", &node) != B_OK
					|| device != fNodeRef->device
					|| node != fNodeRef->node)
					break;
			}
			ApplyShortcutPreferences(true);
			break;
		}
		case B_NODE_MONITOR:
			PRINT(("will update addon shortcuts\n"));
			InitAddOnsList(true);
			ApplyShortcutPreferences(true);
			break;

		default:
			_inherited::MessageReceived(message);
			break;
	}
}
