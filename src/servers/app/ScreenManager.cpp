/*
 * Copyright 2005-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/**	Manages all available physical screens */


#include "ScreenManager.h"


static const uint32 kScreenChangedMessage = 'scch';
static const uint32 kScanGraphicsMessage = 'scgr';
static const uint64 kEnumeratorOutputID = B_MULTI_OUTPUT_INVALID_ID;

#include "Screen.h"
#include "ServerConfig.h"

#include "RemoteHWInterface.h"

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <NodeMonitor.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Path.h>
#include <unistd.h>

#include <new>

using std::nothrow;


static bool
IsEnumeratorScreen(Screen* screen)
{
#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	if (screen == NULL)
		return false;
	AccelerantHWInterface* interface = dynamic_cast<AccelerantHWInterface*>(
		screen->HWInterface());
	return interface != NULL && interface->UsesMultiOutput()
		&& interface->OutputID() == B_MULTI_OUTPUT_INVALID_ID;
#else
	return false;
#endif
}


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
	fScreenList(4),
	fScanRunner(NULL)
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

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	// Connector hotplug does not necessarily create/remove a /dev/graphics
	// node. Poll the accelerant generation so unplug/replug is recovered even
	// when the device path itself remains stable.
	BMessage scanMessage(kScanGraphicsMessage);
	fScanRunner = new(nothrow) BMessageRunner(BMessenger(this), &scanMessage,
		1000000);
#endif
}


ScreenManager::~ScreenManager()
{
	delete fScanRunner;

	// Shared accelerant sessions must outlive all output wrappers. Tear down
	// rendered outputs first and the non-rendering enumerators last, without
	// calling back into Desktop during server shutdown.
	for (int pass = 0; pass < 2; pass++) {
		for (int32 i = 0; i < fScreenList.CountItems();) {
			screen_item* item = fScreenList.ItemAt(i);
			bool isEnumerator = item != NULL
				&& IsEnumeratorScreen(item->screen.Get());
			if ((pass == 0 && isEnumerator) || (pass == 1 && !isEnumerator)) {
				i++;
				continue;
			}
			item = fScreenList.RemoveItem(i);
			if (item == NULL)
				continue;
			item->owner = NULL;
			if (item->screen.Get() != NULL
				&& item->listener.Get() != NULL) {
				HWInterface* interface = item->screen->HWInterface();
				if (interface != NULL)
					interface->RemoveListener(item->listener.Get());
			}
			delete item;
		}
	}

	for (size_t i = 0; i < fWatchedDirectories.size(); i++)
		unwatch_node(&fWatchedDirectories[i], this);
}


Screen*
ScreenManager::ScreenAt(int32 index) const
{
	if (!IsLocked())
		debugger("Called ScreenManager::ScreenAt() without lock!");
	if (index < 0)
		return NULL;

	int32 outputIndex = 0;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item == NULL || item->screen.Get() == NULL
			|| item->removing || IsEnumeratorScreen(item->screen.Get())) {
			continue;
		}
		if (outputIndex++ == index)
			return item->screen.Get();
	}
	return NULL;
}


int32
ScreenManager::CountScreens() const
{
	if (!IsLocked())
		debugger("Called ScreenManager::CountScreens() without lock!");

	int32 count = 0;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item != NULL && item->screen.Get() != NULL
			&& !item->removing && !IsEnumeratorScreen(item->screen.Get())) {
			count++;
		}
	}
	return count;
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

		if (!item->removing && item->owner == NULL
			&& !IsEnumeratorScreen(item->screen.Get())
			&& list.AddItem(item->screen.Get())) {
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

			if (item->screen.Get() == screen && !item->removing)
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
		if (item->screen.Get() == screen && !item->removing) {
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

	ScreenOwner* owner = NULL;
	{
		// Reserve the entry under the manager lock, then release that lock
		// before entering Desktop. Desktop operations may acquire the manager
		// while holding window/screen locks, so calling the owner under this
		// lock would invert the normal lock order and deadlock hot-unplug.
		BAutolock locker(this);
		for (int32 i = 0; i < fScreenList.CountItems(); i++) {
			screen_item* item = fScreenList.ItemAt(i);
			if (item->screen.Get() != screen)
				continue;
			if (item->removing)
				return B_BUSY;

			item->removing = true;
			owner = item->owner;
			item->owner = NULL;
			break;
		}
	}
	if (owner == NULL) {
		// The entry may still exist with no owner (for example during startup);
		// it is still removed below. Distinguish that from an unknown screen.
		bool found = false;
		BAutolock locker(this);
		for (int32 i = 0; i < fScreenList.CountItems(); i++) {
			screen_item* item = fScreenList.ItemAt(i);
			if (item->screen.Get() == screen && item->removing) {
				found = true;
				break;
			}
		}
		if (!found)
			return B_ENTRY_NOT_FOUND;
	}
	if (owner != NULL)
		owner->ScreenRemoved(screen);

	BAutolock locker(this);
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen.Get() == screen && item->removing) {
			item = fScreenList.RemoveItem(i);
			delete item;
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


void
ScreenManager::ScreenChanged(Screen* screen)
{
	if (screen == NULL)
		return;

	// Hardware notifications may arrive while Desktop holds its window/screen
	// locks. Do not call back into Desktop while holding the manager lock (or
	// synchronously from the hardware-notification stack): queue an ID-only
	// notification and resolve it on the ScreenManager looper instead.
	BMessage message(kScreenChangedMessage);
	if (message.AddInt32("screen_id", screen->ID()) == B_OK)
		PostMessage(&message);
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
	// Output wrappers borrow the accelerant image from the persistent
	// enumerator. Remove those wrappers first and the enumerator last when a
	// device node disappears, so no live wrapper calls an unloaded image.
	for (int pass = 0; pass < 2; pass++) {
		for (int32 i = 0; i < fScreenList.CountItems();) {
			screen_item* item = fScreenList.ItemAt(i);
			AccelerantHWInterface* accelerant =
				dynamic_cast<AccelerantHWInterface*>(
					item->screen->HWInterface());
			const char* path = accelerant != NULL
				? accelerant->DevicePath() : NULL;
			bool missing = path != NULL && path[0] != '\0'
				&& access(path, F_OK) != 0;
			if (!missing) {
				i++;
				continue;
			}

			bool isEnumerator = accelerant != NULL
				&& accelerant->UsesMultiOutput()
				&& accelerant->OutputID() == kEnumeratorOutputID;
			if ((pass == 0 && isEnumerator) || (pass == 1 && !isEnumerator)) {
				i++;
				continue;
			}

			RemoveScreen(item->screen.Get());
		}
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

	// Reconcile connector topology on every device-tree scan. The graphics
	// device node can remain present while individual monitor connections
	// change, so device-path enumeration alone is not sufficient.
	_ScanMultiOutputs();
#endif
}


void
ScreenManager::_ScanMultiOutputs()
{
#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	std::vector<BString> processedPaths;

	// Process each physical graphics device once, even though a device with
	// independent outputs now has one Screen/HWInterface per connector.
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item == NULL || item->screen.Get() == NULL)
			continue;

		AccelerantHWInterface* accelerant = dynamic_cast<AccelerantHWInterface*>(
			item->screen->HWInterface());
		if (accelerant == NULL || !accelerant->UsesMultiOutput())
			continue;

		BString path(accelerant->DevicePath());
		if (path.Length() == 0)
			continue;

		bool alreadyProcessed = false;
		for (size_t p = 0; p < processedPaths.size(); p++) {
			if (processedPaths[p] == path) {
				alreadyProcessed = true;
				break;
			}
		}
		if (alreadyProcessed)
			continue;

		try {
			processedPaths.push_back(path);
		} catch (...) {
			return;
		}

		uint64 generationBefore = 0;
		status_t generationStatus
			= accelerant->GetMultiOutputGeneration(&generationBefore);
		bool hasGeneration = generationStatus == B_OK;
		if (generationStatus != B_OK && generationStatus != B_NOT_SUPPORTED)
			continue;

		uint32 countBefore = accelerant->MultiOutputCount();
		// A corrupt driver must not be able to force an unbounded allocation.
		if (countBefore > 256)
			continue;

		std::vector<accelerant_output_info> outputs;
		bool stable = true;
		try {
			outputs.reserve(countBefore);
			for (uint32 outputIndex = 0; outputIndex < countBefore;
					outputIndex++) {
				accelerant_output_info info;
				status_t status = accelerant->GetMultiOutputInfo(outputIndex,
					&info);
				if (status != B_OK) {
					stable = false;
					break;
				}
				if (info.connection_state
						!= B_MULTI_OUTPUT_CONNECTION_CONNECTED
					|| (info.capabilities
						& (B_MULTI_OUTPUT_CAPABILITY_INDEPENDENT_MODE
							| B_MULTI_OUTPUT_CAPABILITY_FRAME_BUFFER))
						!= (B_MULTI_OUTPUT_CAPABILITY_INDEPENDENT_MODE
							| B_MULTI_OUTPUT_CAPABILITY_FRAME_BUFFER)) {
					continue;
				}
				for (size_t existing = 0; existing < outputs.size(); existing++) {
					if (outputs[existing].output_id == info.output_id) {
						stable = false;
						break;
					}
				}
				if (!stable)
					break;
				outputs.push_back(info);
			}
		} catch (...) {
			continue;
		}
		if (!stable || accelerant->MultiOutputCount() != countBefore)
			continue;

		if (hasGeneration) {
			uint64 generationAfter = 0;
			if (accelerant->GetMultiOutputGeneration(&generationAfter) != B_OK
				|| generationAfter != generationBefore) {
				continue;
			}
		}

		// Keep one non-rendering handle per device so a later connector
		// connection can be discovered even when every physical output is
		// currently disconnected.
		bool hasEnumerator = false;
		for (int32 j = 0; j < fScreenList.CountItems(); j++) {
			screen_item* current = fScreenList.ItemAt(j);
			AccelerantHWInterface* currentInterface = current != NULL
				? dynamic_cast<AccelerantHWInterface*>(
					current->screen->HWInterface()) : NULL;
			if (currentInterface != NULL
				&& currentInterface->UsesMultiOutput()
				&& path == currentInterface->DevicePath()
				&& currentInterface->OutputID() == kEnumeratorOutputID) {
				hasEnumerator = true;
				break;
			}
		}
		if (!hasEnumerator) {
			HWInterface* enumerator = new(nothrow) AccelerantHWInterface(
				-1, path.String(), kEnumeratorOutputID, true);
			if (enumerator != NULL)
				_AddHWInterface(enumerator);
		}

		AccelerantHWInterface* sharedAccelerant = NULL;
		for (int32 j = 0; j < fScreenList.CountItems(); j++) {
			screen_item* current = fScreenList.ItemAt(j);
			AccelerantHWInterface* currentInterface = current != NULL
				? dynamic_cast<AccelerantHWInterface*>(
					current->screen->HWInterface()) : NULL;
			if (currentInterface != NULL
				&& currentInterface->UsesMultiOutput()
				&& path == currentInterface->DevicePath()
				&& currentInterface->OutputID() == kEnumeratorOutputID) {
				sharedAccelerant = currentInterface;
				break;
			}
		}
		if (sharedAccelerant == NULL)
			continue;

		// Remember the stable generation on the persistent enumerator. A
		// generation change also covers mode/capability changes on an output
		// whose ID and connection state did not change.
		bool generationChanged = false;
		if (hasGeneration) {
			for (int32 j = 0; j < fScreenList.CountItems(); j++) {
				screen_item* current = fScreenList.ItemAt(j);
				AccelerantHWInterface* currentInterface = current != NULL
					? dynamic_cast<AccelerantHWInterface*>(
						current->screen->HWInterface()) : NULL;
				if (currentInterface == NULL
					|| !currentInterface->UsesMultiOutput()
					|| path != currentInterface->DevicePath()
					|| currentInterface->OutputID() != kEnumeratorOutputID) {
					continue;
				}
				generationChanged = current->hasOutputGeneration
					&& current->outputGeneration != generationBefore;
				current->outputGeneration = generationBefore;
				current->hasOutputGeneration = true;
				break;
			}
		}

		// Detach outputs that disappeared or no longer provide the independent
		// mode/framebuffer contract. The enumerator remains alive for recovery.
		for (int32 j = 0; j < fScreenList.CountItems();) {
			screen_item* current = fScreenList.ItemAt(j);
			AccelerantHWInterface* currentInterface = current != NULL
				? dynamic_cast<AccelerantHWInterface*>(
					current->screen->HWInterface()) : NULL;
			if (currentInterface == NULL || !currentInterface->UsesMultiOutput()
				|| path != currentInterface->DevicePath()
				|| currentInterface->OutputID() == kEnumeratorOutputID) {
				j++;
				continue;
			}

			bool stillConnected = false;
			for (size_t k = 0; k < outputs.size(); k++) {
				if (outputs[k].output_id == currentInterface->OutputID()) {
					stillConnected = true;
					break;
				}
			}
			if (!stillConnected) {
				RemoveScreen(current->screen.Get());
				continue;
			}
			j++;
		}

		// Add each newly connected output as its own Screen and framebuffer
		// interface. Output IDs are stable identifiers, never list indices.
		for (size_t k = 0; k < outputs.size(); k++) {
			bool exists = false;
			for (int32 j = 0; j < fScreenList.CountItems(); j++) {
				screen_item* current = fScreenList.ItemAt(j);
				AccelerantHWInterface* currentInterface = current != NULL
					? dynamic_cast<AccelerantHWInterface*>(
						current->screen->HWInterface()) : NULL;
				if (currentInterface != NULL
					&& currentInterface->UsesMultiOutput()
					&& path == currentInterface->DevicePath()
					&& currentInterface->OutputID() == outputs[k].output_id) {
					exists = true;
					break;
				}
			}
			if (exists)
				continue;

			HWInterface* outputInterface = new(nothrow) AccelerantHWInterface(
				-1, path.String(), outputs[k].output_id, true,
				sharedAccelerant);
			if (outputInterface != NULL)
				_AddHWInterface(outputInterface);
		}

		if (generationChanged) {
			// Queue notifications after reconciliation, so Desktop sees a
			// complete, stable output set before rebuilding mode-dependent state.
			for (int32 j = 0; j < fScreenList.CountItems(); j++) {
				screen_item* current = fScreenList.ItemAt(j);
				AccelerantHWInterface* currentInterface = current != NULL
					? dynamic_cast<AccelerantHWInterface*>(
						current->screen->HWInterface()) : NULL;
				if (currentInterface != NULL && current->owner != NULL
					&& currentInterface->UsesMultiOutput()
					&& path == currentInterface->DevicePath()
					&& currentInterface->OutputID() != kEnumeratorOutputID) {
					bool ignoredModeChange = false;
					currentInterface->CheckOutputModeChanged(&ignoredModeChange);
					ScreenChanged(current->screen.Get());
				}
			}
		} else {
			// The generation hook is optional. Detect a mode change by polling
			// the current mode for each stable output ID when no topology change
			// was reported by the accelerant.
			for (int32 j = 0; j < fScreenList.CountItems(); j++) {
				screen_item* current = fScreenList.ItemAt(j);
				AccelerantHWInterface* currentInterface = current != NULL
					? dynamic_cast<AccelerantHWInterface*>(
						current->screen->HWInterface()) : NULL;
				if (currentInterface == NULL || current->owner == NULL
					|| !currentInterface->UsesMultiOutput()
					|| path != currentInterface->DevicePath()
					|| currentInterface->OutputID() == kEnumeratorOutputID) {
					continue;
				}
				bool modeChanged = false;
				if (currentInterface->CheckOutputModeChanged(&modeChanged) == B_OK
					&& modeChanged) {
					ScreenChanged(current->screen.Get());
				}
			}
		}
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
			item->removing = false;
			item->outputGeneration = 0;
			item->hasOutputGeneration = false;
			item->listener.SetTo(
				new(nothrow) ScreenChangeListener(*this, item->screen.Get()));
			if (item->listener.IsSet()
				&& interface->AddListener(item->listener.Get())) {
				if (fScreenList.AddItem(item)) {
					if (IsEnumeratorScreen(item->screen.Get()))
						return item;

					// Offer a newly discovered screen to each existing owner once.
					// The owner reports whether it incorporated the output.
					int32 addedIndex = fScreenList.CountItems() - 1;
					for (int32 i = 0; i < addedIndex; i++) {
						screen_item* existing = fScreenList.ItemAt(i);
						ScreenOwner* owner = existing->owner;
						if (owner == NULL || existing->removing)
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
		case kScanGraphicsMessage:
#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
			_ScanDrivers();
#endif
			break;

		case kScreenChangedMessage:
	{
		int32 screenID;
		if (message->FindInt32("screen_id", &screenID) != B_OK)
			break;

		Screen* screen = NULL;
		ScreenOwner* owner = NULL;
		{
			BAutolock locker(this);
			for (int32 i = 0; i < fScreenList.CountItems(); i++) {
				screen_item* item = fScreenList.ItemAt(i);
				if (item->screen->ID() != screenID || item->removing)
					continue;
				screen = item->screen.Get();
				owner = item->owner;
				break;
			}
		}
		// Never enter Desktop while the ScreenManager lock is held. Resolving
		// by ID also makes a delayed notification harmless after hot-unplug.
		if (screen != NULL && owner != NULL)
			owner->ScreenChanged(screen);
		break;
	}

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
