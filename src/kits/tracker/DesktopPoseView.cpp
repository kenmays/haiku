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

// DesktopPoseView adds support for displaying integrated desktops
// from multiple volumes to BPoseView
//
// Used by the Desktop window and by the root view in file panels


#include "DesktopPoseView.h"

#include <NodeMonitor.h>
#include <Path.h>
#include <Screen.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "Background.h"
#include "Commands.h"
#include "FSUtils.h"
#include "PoseList.h"
#include "Tracker.h"
#include "TrackerSettings.h"
#include "TrackerString.h"


//	#pragma mark - DesktopPoseView


DesktopPoseView::DesktopPoseView(Model* model, uint32 viewMode)
	:
	BPoseView(model, viewMode)
{
	SetFlags(Flags() | B_DRAW_ON_CHILDREN);
}


void
DesktopPoseView::AttachedToWindow()
{
	AddFilter(new TPoseViewFilter(this));

	_inherited::AttachedToWindow();
}


void
DesktopPoseView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_WORKSPACE_ACTIVATED:
		{
			bool active;
			int32 workspace;
			if (message->FindBool("active", &active) != B_OK || !active
				|| message->FindInt32("workspace", &workspace) != B_OK
				|| workspace != current_workspace()) {
				break;
			}
		} // fall-through
		case B_RESTORE_BACKGROUND_IMAGE:
			AdoptSystemColors();
			Invalidate();
			break;

		default:
			BPoseView::MessageReceived(message);
			break;
	}
}


EntryListBase*
DesktopPoseView::InitDesktopDirentIterator(BPoseView* nodeMonitoringTarget,
	const entry_ref* ref)
{
	// the desktop dirent iterator knows how to iterate over all the volumes,
	// integrated onto the desktop

	Model sourceModel(ref, false, true);
	if (sourceModel.InitCheck() != B_OK)
		return NULL;

	CachedEntryIteratorList* result = new CachedEntryIteratorList();

	ASSERT(!sourceModel.IsQuery());
	ASSERT(!sourceModel.IsVirtualDirectory());
	ASSERT(sourceModel.Node() != NULL);

	BDirectory* sourceDirectory = dynamic_cast<BDirectory*>(sourceModel.Node());
	ThrowOnAssert(sourceDirectory != NULL);

	// build an iterator list, start with boot
	EntryListBase* perDesktopIterator
		= new CachedDirectoryEntryList(*sourceDirectory);

	result->AddItem(perDesktopIterator);
	if (nodeMonitoringTarget != NULL) {
		TTracker::WatchNode(sourceModel.NodeRef(), B_WATCH_DIRECTORY | B_WATCH_CHILDREN
				| B_WATCH_NAME | B_WATCH_STAT | B_WATCH_INTERIM_STAT | B_WATCH_ATTR,
			nodeMonitoringTarget);
	}

	if (result->Rewind() != B_OK) {
		delete result;
		if (nodeMonitoringTarget != NULL)
			nodeMonitoringTarget->HideBarberPole();

		return NULL;
	}

	return result;
}


void
DesktopPoseView::AdoptSystemColors()
{
	BScreen screen(Window());
	rgb_color background = screen.DesktopColor();
	SetLowColor(background);
	SetViewColor(background);

	AdaptToBackgroundColorChange();
}


bool
DesktopPoseView::HasSystemColors() const
{
	return false;
}


EntryListBase*
DesktopPoseView::InitDirentIterator(const entry_ref* ref)
{
	return InitDesktopDirentIterator(this, ref);
}


bool
DesktopPoseView::FSNotification(const BMessage* message)
{
	switch (message->FindInt32("opcode")) {
		case B_DEVICE_MOUNTED:
		{
			dev_t device;
			if (message->FindInt32("new device", &device) != B_OK)
				break;

			ASSERT(TargetModel());
			TrackerSettings settings;

			BVolume volume(device);
			if (volume.InitCheck() != B_OK)
				break;

			if (settings.MountVolumesOntoDesktop()
				&& (!volume.IsShared()
					|| settings.MountSharedVolumesOntoDesktop())) {
				// place an icon for the volume onto the desktop
				CreateVolumePose(&volume, true);
			}
		}
		break;
	}

	return _inherited::FSNotification(message);
}


bool
DesktopPoseView::AddPosesThreadValid(const entry_ref*) const
{
	return true;
}


void
DesktopPoseView::AddPosesCompleted()
{
	_inherited::AddPosesCompleted();
	CreateTrashPose();
}


bool
DesktopPoseView::Represents(const node_ref* ref) const
{
	// When the Tracker is set up to integrate non-boot beos volumes,
	// it represents the home/Desktop folders of all beos volumes

	return _inherited::Represents(ref);
}


bool
DesktopPoseView::Represents(const entry_ref* ref) const
{
	BEntry entry(ref);
	node_ref nref;
	entry.GetNodeRef(&nref);
	return Represents(&nref);
}


void
DesktopPoseView::ShowVolumes(bool visible, bool showShared)
{
	if (LockLooper()) {
		SavePoseLocations();
		if (!visible)
			RemoveRootPoses();
		else
			AddRootPoses(true, showShared);

		UnlockLooper();
	}
}


void
DesktopPoseView::StartSettingsWatch()
{
	if (be_app->LockLooper()) {
		be_app->StartWatching(this, kShowDisksIconChanged);
		be_app->StartWatching(this, kVolumesOnDesktopChanged);
		be_app->StartWatching(this, kDesktopIntegrationChanged);
		be_app->UnlockLooper();
	}
}


void
DesktopPoseView::StopSettingsWatch()
{
	if (be_app->LockLooper()) {
		be_app->StopWatching(this, kShowDisksIconChanged);
		be_app->StopWatching(this, kVolumesOnDesktopChanged);
		be_app->StopWatching(this, kDesktopIntegrationChanged);
		be_app->UnlockLooper();
	}
}


void
DesktopPoseView::AdaptToVolumeChange(BMessage* message)
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	ThrowOnAssert(tracker != NULL);

	bool showDisksIcon = false;
	bool mountVolumesOnDesktop = true;
	bool mountSharedVolumesOntoDesktop = false;

	message->FindBool("ShowDisksIcon", &showDisksIcon);
	message->FindBool("MountVolumesOntoDesktop", &mountVolumesOnDesktop);
	message->FindBool("MountSharedVolumesOntoDesktop",
		&mountSharedVolumesOntoDesktop);

	BEntry entry("/");
	Model model(&entry);
	if (model.InitCheck() == B_OK) {
		BMessage entryMessage;
		entryMessage.what = B_NODE_MONITOR;

		if (showDisksIcon)
			entryMessage.AddInt32("opcode", B_ENTRY_CREATED);
		else {
			entryMessage.AddInt32("opcode", B_ENTRY_REMOVED);
			entry_ref ref;
			if (entry.GetRef(&ref) == B_OK) {
				BContainerWindow* disksWindow
					= tracker->FindContainerWindow(&ref);
				if (disksWindow != NULL) {
					disksWindow->Lock();
					disksWindow->Close();
				}
			}
		}
		entryMessage.AddInt32("device", model.NodeRef()->device);
		entryMessage.AddInt64("node", model.NodeRef()->node);
		entryMessage.AddInt64("directory", model.EntryRef()->directory);
		entryMessage.AddString("name", model.EntryRef()->name);
		BContainerWindow* deskWindow
			= dynamic_cast<BContainerWindow*>(Window());
		if (deskWindow != NULL)
			deskWindow->PostMessage(&entryMessage, deskWindow->PoseView());
	}

	ShowVolumes(mountVolumesOnDesktop, mountSharedVolumesOntoDesktop);
}


void
DesktopPoseView::AdaptToDesktopIntegrationChange(BMessage* message)
{
	bool mountVolumesOnDesktop = true;
	bool mountSharedVolumesOntoDesktop = true;

	message->FindBool("MountVolumesOntoDesktop", &mountVolumesOnDesktop);
	message->FindBool("MountSharedVolumesOntoDesktop",
		&mountSharedVolumesOntoDesktop);

	ShowVolumes(false, mountSharedVolumesOntoDesktop);
	ShowVolumes(mountVolumesOnDesktop, mountSharedVolumesOntoDesktop);
}


void
DesktopPoseView::AdaptToBackgroundColorChange()
{
	// The Desktop text color is chosen independently for the Desktop.
	// The text color is chosen globally for all directories.
	// It's fairly easy to get something unreadable (even with the default
	// settings, it's expected that text will be black on white in Tracker
	// folders, but white on blue on the desktop).

	int32 desktopBrightness = ui_color(B_DESKTOP_COLOR).Brightness();
	SetHighColor(LowColor().Brightness() <= desktopBrightness ? kWhite : kBlack);
}
