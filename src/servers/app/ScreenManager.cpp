/*
 * Copyright 2005-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/**	Manages all available physical screens */


#include "ScreenManager.h"

#include "Screen.h"
#include "ServerConfig.h"

#include "RemoteHWInterface.h"

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <unistd.h>

#include <new>

using std::nothrow;


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	include "AccelerantHWInterface.h"
#else
#	include "ViewHWInterface.h"
#	include "DWindowHWInterface.h"
#endif


ScreenManager* gScreenManager;


class ScreenChangeListener : public HWInterfaceListener {
public:
								ScreenChangeListener(ScreenManager& manager,
									Screen* screen);

private:
virtual	void					ScreenChanged(HWInterface* interface);

			ScreenManager&		fManager;
			Screen*				fScreen;
};


ScreenChangeListener::ScreenChangeListener(ScreenManager& manager,
	Screen* screen)
	:
	fManager(manager),
	fScreen(screen)
{
}


void
ScreenChangeListener::ScreenChanged(HWInterface* interface)
{
	fManager.ScreenChanged(fScreen);
}


ScreenManager::ScreenManager()
	:
	BLooper("screen manager"),
	fNextScreenID(0),
	fKnownGraphicsDeviceCount(0),
	fScreenList(4)
{
#ifdef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	if defined(USE_DIRECT_WINDOW_TEST_MODE)
	_AddHWInterface(new DWindowHWInterface());
#	else
	_AddHWInterface(new ViewHWInterface());
#	endif
#else
	_ScanDrivers();

	// Watch the complete graphics-device tree. Some drivers publish their
	// device nodes in vendor subdirectories, so watching only /dev/graphics
	// misses hotplug events below that directory.
	_WatchGraphicsDirectories("/dev/graphics");
#endif
}


ScreenManager::~ScreenManager()
{
	for (size_t i = 0; i < fWatchedDirectories.size(); i++)
		unwatch_node(&fWatchedDirectories[i], this);
}


Screen*
ScreenManager::ScreenAt(int32 index) const
{
	if (!IsLocked())
		debugger("Called ScreenManager::ScreenAt() without lock!");

	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen.Get();

	return NULL;
}


int32
ScreenManager::CountScreens() const
{
	if (!IsLocked())
		debugger("Called ScreenManager::CountScreens() without lock!");

	return fScreenList.CountItems();
}


status_t
ScreenManager::AcquireScreens(ScreenOwner* owner, int32* wishList,
	int32 wishCount, const char* target, bool force, ScreenList& list)
{
	BAutolock locker(this);
	int32 added = 0;

	// TODO: don't ignore the wish list

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		if (item->owner == NULL && list.AddItem(item->screen.Get())) {
			item->owner = owner;
			added++;
		}
	}

	if (added == 0 && target != NULL) {
		// there's a specific target screen we want to initialize
		// TODO: right now we only support remote screens, but we could
		// also target specific accelerants to support other graphics cards
		HWInterface* interface;
#ifdef HAIKU_TARGET_PLATFORM_LIBBE_TEST
		interface = new(nothrow) ViewHWInterface();
#else
		interface = new(nothrow) RemoteHWInterface(target);
#endif
		if (interface != NULL) {
			screen_item* item = _AddHWInterface(interface);
			if (item != NULL && list.AddItem(item->screen.Get())) {
				item->owner = owner;
				added++;
			}
		}
	}

	return added > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


void
ScreenManager::ReleaseScreens(ScreenList& list)
{
	BAutolock locker(this);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		for (int32 j = 0; j < list.CountItems(); j++) {
			Screen* screen = list.ItemAt(j);

			if (item->screen.Get() == screen)
				item->owner = NULL;
		}
	}
}


void
ScreenManager::ReleaseScreen(Screen* screen)
{
	if (screen == NULL)
		return;

	BAutolock locker(this);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen.Get() == screen) {
			item->owner = NULL;
			return;
		}
	}
}


status_t
ScreenManager::RemoveScreen(Screen* screen)
{
	if (screen == NULL)
		return B_BAD_VALUE;

	BAutolock locker(this);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen.Get() != screen)
			continue;

		// Detach the screen from its owner before destroying its HWInterface.
		// The owner callback must not call back into ScreenManager; VirtualScreen
		// removal deliberately avoids ReleaseScreen() for this reason.
		if (item->owner != NULL) {
			ScreenOwner* owner = item->owner;
			owner->ScreenRemoved(screen);
			item->owner = NULL;
		}

		item = fScreenList.RemoveItem(i);
		delete item;
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


void
ScreenManager::ScreenChanged(Screen* screen)
{
	BAutolock locker(this);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen.Get() == screen && item->owner != NULL)
			item->owner->ScreenChanged(screen);
	}
}


void
ScreenManager::_ScanDrivers()
{
#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	// Enumerate by stable device path, not by the current ordinal. Device
	// indices shift when a graphics node is removed; reopening by path avoids
	// aliasing a surviving output or overlooking a newly-added one.
	int32 deviceCount = AccelerantHWInterface::CountGraphicsDevices();
	if (deviceCount < 0)
		deviceCount = 0;

	// Reconcile existing screens against their stable device paths on every
	// scan, not only in response to B_ENTRY_REMOVED. Node-monitor notifications
	// can be coalesced or missed while app_server is busy; leaving a dead
	// framebuffer attached would keep stale Screen/HWInterface pointers in
	// windows and the virtual scene. RemoveScreen() detaches the owner before
	// the Screen and its interface are released.
	for (int32 i = 0; i < fScreenList.CountItems();) {
		screen_item* item = fScreenList.ItemAt(i);
		AccelerantHWInterface* accelerant =
			dynamic_cast<AccelerantHWInterface*>(
				item->screen->HWInterface());
		const char* path = accelerant != NULL
			? accelerant->DevicePath() : NULL;
		if (path != NULL && path[0] != '\\0' && access(path, F_OK) != 0) {
			RemoveScreen(item->screen.Get());
			continue;
		}
		i++;
	}

	// If a real graphics node has appeared after the fallback backend was
	// selected, detach the fallback first so it cannot remain a phantom output.
	if (deviceCount > 0) {
		for (int32 i = 0; i < fScreenList.CountItems();) {
			screen_item* item = fScreenList.ItemAt(i);
			AccelerantHWInterface* accelerant =
				dynamic_cast<AccelerantHWInterface*>(
					item->screen->HWInterface());
			if (accelerant != NULL && accelerant->DevicePath()[0] == '\0') {
				RemoveScreen(item->screen.Get());
				continue;
			}
			i++;
		}
	}

	for (int32 i = 0; i < deviceCount; i++) {
		BString devicePath;
		if (AccelerantHWInterface::GetGraphicsDevicePath(i, devicePath) != B_OK)
			continue;

		bool alreadyOpen = false;
		for (int32 j = 0; j < fScreenList.CountItems(); j++) {
			screen_item* item = fScreenList.ItemAt(j);
			AccelerantHWInterface* existing =
				dynamic_cast<AccelerantHWInterface*>(
					item->screen->HWInterface());
			if (existing != NULL && devicePath == existing->DevicePath()) {
				alreadyOpen = true;
				break;
			}
		}
		if (alreadyOpen)
			continue;

		HWInterface* interface = new(nothrow) AccelerantHWInterface(
			-1, devicePath.String());
		if (interface != NULL)
			_AddHWInterface(interface);
	}

	fKnownGraphicsDeviceCount = deviceCount;

	// Preserve VESA/framebuffer fallback when no enumerated device initializes,
	// including safe-video mode where accelerant enumeration is disabled.
	if (fScreenList.CountItems() == 0) {
		HWInterface* interface = new(nothrow) AccelerantHWInterface();
		if (interface != NULL)
			_AddHWInterface(interface);
	}
#endif
}


ScreenManager::screen_item*
ScreenManager::_AddHWInterface(HWInterface* interface)
{
	ObjectDeleter<Screen> screen(
		new(nothrow) Screen(interface, fNextScreenID++));
	if (!screen.IsSet()) {
		delete interface;
		return NULL;
	}

	// The interface is now owned by the screen

	if (screen->Initialize() >= B_OK) {
		screen_item* item = new(nothrow) screen_item;

		if (item != NULL) {
			item->screen.SetTo(screen.Detach());
			item->owner = NULL;
			item->listener.SetTo(
				new(nothrow) ScreenChangeListener(*this, item->screen.Get()));
			if (item->listener.IsSet()
				&& interface->AddListener(item->listener.Get())) {
				if (fScreenList.AddItem(item)) {
					// Offer a newly discovered screen to each existing owner once.
					// The owner reports whether it incorporated the output.
					int32 addedIndex = fScreenList.CountItems() - 1;
					for (int32 i = 0; i < addedIndex; i++) {
						screen_item* existing = fScreenList.ItemAt(i);
						ScreenOwner* owner = existing->owner;
						if (owner == NULL)
							continue;

						bool alreadyTried = false;
						for (int32 j = 0; j < i; j++) {
							screen_item* previous = fScreenList.ItemAt(j);
							if (previous->owner == owner) {
								alreadyTried = true;
								break;
							}
						}
						if (alreadyTried)
							continue;

						if (owner->ScreenAdded(item->screen.Get())) {
							item->owner = owner;
							break;
						}
					}
					return item;
				}

				interface->RemoveListener(item->listener.Get());
			}

			delete item;
		}
	}

	return NULL;
}


void
ScreenManager::_WatchGraphicsDirectories(const char* path)
{
	if (path == NULL || path[0] == '\0')
		return;

	BEntry entry(path);
	node_ref nodeRef;
	if (entry.InitCheck() != B_OK || entry.GetNodeRef(&nodeRef) != B_OK)
		return;

	bool alreadyWatched = false;
	for (size_t i = 0; i < fWatchedDirectories.size(); i++) {
		if (fWatchedDirectories[i].device == nodeRef.device
			&& fWatchedDirectories[i].node == nodeRef.node) {
			alreadyWatched = true;
			break;
		}
	}

	if (!alreadyWatched
		&& watch_node(&nodeRef, B_WATCH_DIRECTORY, this) == B_OK) {
		try {
			fWatchedDirectories.push_back(nodeRef);
		} catch (...) {
			unwatch_node(&nodeRef, this);
		}
	}

	BDirectory directory(path);
	BEntry child;
	while (directory.GetNextEntry(&child, false) == B_OK) {
		if (!child.IsDirectory() || child.IsSymLink())
			continue;

		BPath childPath;
		if (child.GetPath(&childPath) == B_OK)
			_WatchGraphicsDirectories(childPath.Path());
	}
}


void
ScreenManager::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
		{
			int32 opcode;
			if (message->FindInt32("opcode", &opcode) == B_OK) {
				if (opcode == B_ENTRY_REMOVED) {
					// Remove only accelerant-backed screens whose stable node path
					// disappeared. ScreenRemoved detaches the output before the
					// Screen and its framebuffer interface are destroyed.
					for (int32 i = 0; i < fScreenList.CountItems();) {
						screen_item* item = fScreenList.ItemAt(i);
						AccelerantHWInterface* accelerant =
							dynamic_cast<AccelerantHWInterface*>(
								item->screen->HWInterface());
						const char* path = accelerant != NULL
							? accelerant->DevicePath() : NULL;
						if (path != NULL && path[0] != '\0'
							&& access(path, F_OK) != 0) {
							RemoveScreen(item->screen.Get());
							continue;
						}
						i++;
					}
					fKnownGraphicsDeviceCount
						= AccelerantHWInterface::CountGraphicsDevices();
				}
				if (opcode == B_ENTRY_CREATED || opcode == B_ENTRY_REMOVED) {
					_WatchGraphicsDirectories("/dev/graphics");
					_ScanDrivers();
				}
			}
		}
#endif
			break;

		default:
			BHandler::MessageReceived(message);
	}
}
