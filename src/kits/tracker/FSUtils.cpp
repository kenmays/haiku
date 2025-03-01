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
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
trademarks of Be Incorporated in the United States and other countries. Other
brand product names are registered trademarks or trademarks of their
respective holders. All rights reserved.
*/

// Tracker file system calls.

// APIs/code in FSUtils.h and FSUtils.cpp is slated for a major cleanup -- in
// other words, you will find a lot of ugly cruft in here

// ToDo:
// Move most of preflight error checks to the Model level and only keep those
// that have to do with size, reading/writing and name collisions.
// Get rid of all the BList based APIs, use BObjectLists.
// Clean up the error handling, push most of the user interaction out of the
// low level FS calls.


#include <ctype.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>
#include <String.h>
#include <StringFormat.h>
#include <SymLink.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <fs_attr.h>
#include <fs_info.h>
#include <sys/utsname.h>

#include <AutoLocker.h>
#include <libroot/libroot_private.h>
#include <system/syscalls.h>
#include <system/syscall_load_image.h>

#include "Attributes.h"
#include "Bitmaps.h"
#include "Commands.h"
#include "FindPanel.h"
#include "FSUndoRedo.h"
#include "FSUtils.h"
#include "InfoWindow.h"
#include "MimeTypes.h"
#include "OverrideAlert.h"
#include "StatusWindow.h"
#include "Thread.h"
#include "Tracker.h"
#include "TrackerSettings.h"
#include "Utilities.h"
#include "VirtualDirectoryManager.h"


enum {
	kUserCanceled = B_ERRORS_END + 1,
	kCopyCanceled = kUserCanceled,
	kTrashCanceled
};

enum ConflictCheckResult {
	kCanceled = kUserCanceled,
	kPrompt,
	kSkipAll,
	kReplace,
	kReplaceAll,
	kNoConflicts
};


namespace BPrivate {

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FSUtils"

static status_t FSDeleteFolder(BEntry*, CopyLoopControl*, bool updateStatus,
	bool deleteTopDir = true, bool upateFileNameInStatus = false);
static status_t MoveEntryToTrash(BEntry*, BPoint*, Undo &undo);
static void LowLevelCopy(BEntry*, StatStruct*, BDirectory*, char* destName,
	CopyLoopControl*, BPoint*);
status_t DuplicateTask(BObjectList<entry_ref, true>* srcList);
static status_t MoveTask(BObjectList<entry_ref, true>*, BEntry*, BList*, uint32);
static status_t _DeleteTask(BObjectList<entry_ref, true>*, bool);
static status_t _RestoreTask(BObjectList<entry_ref, true>*);
status_t CalcItemsAndSize(CopyLoopControl* loopControl,
	BObjectList<entry_ref, true>* refList, ssize_t blockSize, int32* totalCount,
	off_t* totalSize);
status_t MoveItem(BEntry* entry, BDirectory* destDir, BPoint* loc,
	uint32 moveMode, const char* newName, Undo &undo,
	CopyLoopControl* loopControl);
ConflictCheckResult PreFlightNameCheck(BObjectList<entry_ref, true>* srcList,
	const BDirectory* destDir, int32* collisionCount, uint32 moveMode);
status_t CheckName(uint32 moveMode, const BEntry* srcEntry,
	const BDirectory* destDir, bool multipleCollisions,
	ConflictCheckResult &);
void CopyAttributes(CopyLoopControl* control, BNode* srcNode,
	BNode* destNode, void* buffer, size_t bufsize);
void CopyPoseLocation(BNode* src, BNode* dest);
bool DirectoryMatchesOrContains(const BEntry*, directory_which);
bool DirectoryMatchesOrContains(const BEntry*, const char* additionalPath,
	directory_which);
bool DirectoryMatches(const BEntry*, directory_which);
bool DirectoryMatches(const BEntry*, const char* additionalPath,
	directory_which);

status_t empty_trash(void*);


static const char* kDeleteConfirmationStr =
	B_TRANSLATE_MARK("Are you sure you want to delete the "
	"selected item(s)? This operation cannot be reverted.");

static const char* kReplaceStr =
	B_TRANSLATE_MARK("You are trying to replace the item:\n"
	"\t%name%dest\n"
	"with:\n"
	"\t%name%src\n\n"
	"Would you like to replace it with the one you are %movemode?");

static const char* kDirectoryReplaceStr =
	B_TRANSLATE_MARK("An item named \"%name\" already exists in "
	"this folder, and may contain\nitems with the same names. Would you like "
	"to replace them with those contained in the folder you are %verb?");

static const char* kSymLinkReplaceStr =
	B_TRANSLATE_MARK("An item named \"%name\" already exists in this "
	"folder. Would you like to replace it with the symbolic link you are "
	"creating?");

static const char* kNoFreeSpace =
	B_TRANSLATE_MARK("Sorry, there is not enough free space on the "
	"destination volume to copy the selection.");

static const char* kFileErrorString =
	B_TRANSLATE_MARK("Error copying file \"%name\":\n\t%error\n\n"
	"Would you like to continue?");

static const char* kFolderErrorString =
	B_TRANSLATE_MARK("Error copying folder \"%name\":\n\t%error\n\n"
	"Would you like to continue?");

static const char* kFileDeleteErrorString =
	B_TRANSLATE_MARK("There was an error deleting \"%name\""
	":\n\t%error");

static const char* kReplaceManyStr =
	B_TRANSLATE_MARK("Some items already exist in this folder with "
	"the same names as the items you are %verb.\n \nWould you like to "
	"replace them with the ones you are %verb or be prompted for each "
	"one?");

static const char* kFindAlternativeStr =
	B_TRANSLATE_MARK("Would you like to find some other suitable "
	"application?");

static const char* kFindApplicationStr =
	B_TRANSLATE_MARK("Would you like to find a suitable application "
	"to open the file?");


// Skip these attributes when copying in Tracker
const char* kSkipAttributes[] = {
	kAttrPoseInfo,
	NULL
};


// #pragma mark - CopyLoopControl


CopyLoopControl::~CopyLoopControl()
{
}


void
CopyLoopControl::Init(uint32 jobKind)
{
}


void
CopyLoopControl::Init(int32 totalItems, off_t totalSize,
	const entry_ref* destDir, bool showCount)
{
}


bool
CopyLoopControl::FileError(const char* message, const char* name,
	status_t error, bool allowContinue)
{
	return false;
}


void
CopyLoopControl::UpdateStatus(const char* name, const entry_ref& ref,
	int32 count, bool optional)
{
}


bool
CopyLoopControl::CheckUserCanceled()
{
	return false;
}


CopyLoopControl::OverwriteMode
CopyLoopControl::OverwriteOnConflict(const BEntry* srcEntry,
	const char* destName, const BDirectory* destDir, bool srcIsDir,
	bool dstIsDir)
{
	return kReplace;
}


bool
CopyLoopControl::SkipEntry(const BEntry*, bool)
{
	// Tracker makes no exceptions
	return false;
}


void
CopyLoopControl::ChecksumChunk(const char*, size_t)
{
}


bool
CopyLoopControl::ChecksumFile(const entry_ref*)
{
	return true;
}


bool
CopyLoopControl::SkipAttribute(const char*)
{
	return false;
}


bool
CopyLoopControl::PreserveAttribute(const char*)
{
	return false;
}


// #pragma mark - TrackerCopyLoopControl


TrackerCopyLoopControl::TrackerCopyLoopControl()
	:
	fThread(find_thread(NULL)),
	fSourceList(NULL)
{
}


TrackerCopyLoopControl::TrackerCopyLoopControl(uint32 jobKind)
	:
	fThread(find_thread(NULL)),
	fSourceList(NULL)
{
	Init(jobKind);
}


TrackerCopyLoopControl::TrackerCopyLoopControl(int32 totalItems,
		off_t totalSize)
	:
	fThread(find_thread(NULL)),
	fSourceList(NULL)
{
	Init(totalItems, totalSize);
}


TrackerCopyLoopControl::~TrackerCopyLoopControl()
{
	if (gStatusWindow != NULL)
		gStatusWindow->RemoveStatusItem(fThread);
}


void
TrackerCopyLoopControl::Init(uint32 jobKind)
{
	if (gStatusWindow != NULL)
		gStatusWindow->CreateStatusItem(fThread, (StatusWindowState)jobKind);
}


void
TrackerCopyLoopControl::Init(int32 totalItems, off_t totalSize,
	const entry_ref* destDir, bool showCount)
{
	if (gStatusWindow != NULL) {
		gStatusWindow->InitStatusItem(fThread, totalItems, totalSize,
			destDir, showCount);
	}
}


bool
TrackerCopyLoopControl::FileError(const char* message, const char* name,
	status_t error, bool allowContinue)
{
	BString buffer(message);
	buffer.ReplaceFirst("%name", name);
	buffer.ReplaceFirst("%error", strerror(error));

	if (allowContinue) {
		BAlert* alert = new BAlert("", buffer.String(), B_TRANSLATE("Cancel"),
			B_TRANSLATE("OK"), 0, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		return alert->Go() != 0;
	}

	BAlert* alert = new BAlert("", buffer.String(), B_TRANSLATE("Cancel"), 0, 0,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
	return false;
}


void
TrackerCopyLoopControl::UpdateStatus(const char* name, const entry_ref&,
	int32 count, bool optional)
{
	if (gStatusWindow != NULL)
		gStatusWindow->UpdateStatus(fThread, name, count, optional);
}


bool
TrackerCopyLoopControl::CheckUserCanceled()
{
	if (gStatusWindow == NULL)
		return false;

	if (gStatusWindow->CheckCanceledOrPaused(fThread))
		return true;

	if (fSourceList != NULL) {
		// TODO: Check if the user dropped additional files onto this job.
//		printf("%p->CheckUserCanceled()\n", this);
	}

	return false;
}


bool
TrackerCopyLoopControl::SkipAttribute(const char* attributeName)
{
	for (const char** skipAttribute = kSkipAttributes; *skipAttribute;
		skipAttribute++) {
		if (strcmp(*skipAttribute, attributeName) == 0)
			return true;
	}

	return false;
}


void
TrackerCopyLoopControl::SetSourceList(EntryList* list)
{
	fSourceList = list;
}


// #pragma mark - the rest


static BNode*
GetWritableNode(BEntry* entry, StatStruct* statBuf = 0)
{
	// utility call that works around the problem with BNodes not being
	// universally writeable
	// BNodes created on files will fail to WriteAttr because they do not
	// have the right r/w permissions

	StatStruct localStatbuf;

	if (!statBuf) {
		statBuf = &localStatbuf;
		if (entry->GetStat(statBuf) != B_OK)
			return 0;
	}

	if (S_ISREG(statBuf->st_mode))
		return new BFile(entry, O_RDWR);

	return new BNode(entry);
}


bool
CheckDevicesEqual(const entry_ref* srcRef, const Model* targetModel)
{
	BDirectory destDir (targetModel->EntryRef());
	struct stat deststat;
	destDir.GetStat(&deststat);

	return srcRef->device == deststat.st_dev;
}


status_t
FSSetPoseLocation(ino_t destDirInode, BNode* destNode, BPoint point)
{
	PoseInfo poseInfo;
	poseInfo.fInvisible = false;
	poseInfo.fInitedDirectory = destDirInode;
	poseInfo.fLocation = point;

	status_t result = destNode->WriteAttr(kAttrPoseInfo, B_RAW_TYPE, 0,
		&poseInfo, sizeof(poseInfo));

	if (result == sizeof(poseInfo))
		return B_OK;

	return result;
}


status_t
FSSetPoseLocation(BEntry* entry, BPoint point)
{
	BNode node(entry);
	status_t result = node.InitCheck();
	if (result != B_OK)
		return result;

	BDirectory parent;
	result = entry->GetParent(&parent);
	if (result != B_OK)
		return result;

	node_ref destNodeRef;
	result = parent.GetNodeRef(&destNodeRef);
	if (result != B_OK)
		return result;

	return FSSetPoseLocation(destNodeRef.node, &node, point);
}


bool
FSGetPoseLocation(const BNode* node, BPoint* point)
{
	PoseInfo poseInfo;
	if (ReadAttr(node, kAttrPoseInfo, kAttrPoseInfoForeign,
		B_RAW_TYPE, 0, &poseInfo, sizeof(poseInfo), &PoseInfo::EndianSwap)
			== kReadAttrFailed) {
		return false;
	}

	if (poseInfo.fInitedDirectory == -1LL)
		return false;

	*point = poseInfo.fLocation;

	return true;
}


static void
SetupPoseLocation(ino_t sourceParentIno, ino_t destParentIno, const BNode* sourceNode,
	BNode* destNode, BPoint* loc)
{
	BPoint point;
	if (loc == NULL
		// we don't have a position yet
		&& sourceParentIno != destParentIno
		// we aren't  copying into the same directory
		&& FSGetPoseLocation(sourceNode, &point)) {
		// the original has a valid inited location
		loc = &point;
		// copy the originals location
	}

	if (loc != NULL && loc != (BPoint*)-1) {
		// loc of -1 is used when copying/moving into a window in list mode
		// where copying positions would not work
		// ToSo:
		// should push all this logic to upper levels
		FSSetPoseLocation(destParentIno, destNode, *loc);
	}
}


void
FSMoveToFolder(BObjectList<entry_ref, true>* srcList, BEntry* destEntry,
	uint32 moveMode, BList* pointList)
{
	if (srcList->IsEmpty()) {
		delete srcList;
		delete pointList;
		delete destEntry;
		return;
	}

	LaunchInNewThread("MoveTask", B_NORMAL_PRIORITY, MoveTask, srcList,
		destEntry, pointList, moveMode);
}


void
FSDelete(entry_ref* ref, bool async, bool confirm)
{
	BObjectList<entry_ref, true>* list = new BObjectList<entry_ref, true>(1);
	list->AddItem(ref);
	FSDeleteRefList(list, async, confirm);
}


void
FSDeleteRefList(BObjectList<entry_ref, true>* list, bool async, bool confirm)
{
	if (async) {
		LaunchInNewThread("DeleteTask", B_NORMAL_PRIORITY, _DeleteTask, list,
			confirm);
	} else
		_DeleteTask(list, confirm);
}


void
FSRestoreRefList(BObjectList<entry_ref, true>* list, bool async)
{
	if (async) {
		LaunchInNewThread("RestoreTask", B_NORMAL_PRIORITY, _RestoreTask,
			list);
	} else
		_RestoreTask(list);
}


void
FSMoveToTrash(BObjectList<entry_ref, true>* srcList, BList* pointList, bool async)
{
	if (srcList->IsEmpty()) {
		delete srcList;
		delete pointList;
		return;
	}

	if (async)
		LaunchInNewThread("MoveTask", B_NORMAL_PRIORITY, MoveTask, srcList,
			(BEntry*)0, pointList, kMoveSelectionTo);
	else
		MoveTask(srcList, 0, pointList, kMoveSelectionTo);
}


enum {
	kNotConfirmed,
	kConfirmedHomeMove,
	kConfirmedAll
};


bool
ConfirmChangeIfWellKnownDirectory(const BEntry* entry, DestructiveAction action,
	bool dontAsk, int32* confirmedAlready)
{
	// Don't let the user casually move/change important files/folders
	//
	// This is a cheap replacement for having a real UID support turned
	// on and not running as root all the time

	if (confirmedAlready && *confirmedAlready == kConfirmedAll)
		return true;

	if (FSIsDeskDir(entry) || FSIsTrashDir(entry) || FSIsRootDir(entry))
		return false;

	if ((!DirectoryMatchesOrContains(entry, B_SYSTEM_DIRECTORY)
		&& !DirectoryMatchesOrContains(entry, B_USER_DIRECTORY))
		|| DirectoryMatchesOrContains(entry, B_SYSTEM_TEMP_DIRECTORY))
		// quick way out
		return true;

	BString warning;
	bool requireOverride = true;

	if (DirectoryMatchesOrContains(entry, B_SYSTEM_DIRECTORY)) {
		if (action == kRename) {
			warning.SetTo(
				B_TRANSLATE("If you rename the system folder or its "
				"contents, you won't be able to boot %osName!\n\nAre you sure "
				"you want to do this?\n\nTo rename the system folder or its "
				"contents anyway, hold down the Shift key and click "
				"\"Rename\"."));
		} else if(action == kMove) {
			warning.SetTo(
				B_TRANSLATE("If you move the system folder or its "
				"contents, you won't be able to boot %osName!\n\nAre you sure "
				"you want to do this?\n\nTo move the system folder or its "
				"contents anyway, hold down the Shift key and click "
				"\"Move\"."));
		} else {
			warning.SetTo(
				B_TRANSLATE("If you alter the system folder or its "
				"contents, you won't be able to boot %osName!\n\nAre you sure "
				"you want to do this?\n\nTo alter the system folder or its "
				"contents anyway, hold down the Shift key and click "
				"\"I know what I'm doing\"."));
		}
	} else if (DirectoryMatches(entry, B_USER_DIRECTORY)) {
		if (action == kRename) {
			warning .SetTo(
				B_TRANSLATE("If you rename the home folder, %osName "
				"may not behave properly!\n\nAre you sure you want to do this?"
				"\n\nTo rename the home folder anyway, hold down the "
				"Shift key and click \"Rename\"."));
		} else if (action == kMove) {
			warning .SetTo(
				B_TRANSLATE("If you move the home folder, %osName "
				"may not behave properly!\n\nAre you sure you want to do this?"
				"\n\nTo move the home folder anyway, hold down the "
				"Shift key and click \"Move\"."));
		} else {
			warning .SetTo(
				B_TRANSLATE("If you alter the home folder, %osName "
				"may not behave properly!\n\nAre you sure you want to do this?"
				"\n\nTo alter the home folder anyway, hold down the "
				"Shift key and click \"I know what I'm doing\"."));
		}
	} else if (DirectoryMatchesOrContains(entry, B_USER_CONFIG_DIRECTORY)
		|| DirectoryMatchesOrContains(entry, B_SYSTEM_SETTINGS_DIRECTORY)) {
		if (action == kRename) {
			warning.SetTo(
				B_TRANSLATE("If you rename %target, %osName may not behave "
				"properly!\n\nAre you sure you want to do this?"));
		} else if (action == kMove) {
			warning.SetTo(
				B_TRANSLATE("If you move %target, %osName may not behave "
				"properly!\n\nAre you sure you want to do this?"));
		} else {
			warning.SetTo(
				B_TRANSLATE("If you alter %target, %osName may not behave "
				"properly!\n\nAre you sure you want to do this?"));
		}

		if (DirectoryMatchesOrContains(entry, "beos_mime",
				B_USER_SETTINGS_DIRECTORY)
			|| DirectoryMatchesOrContains(entry, "beos_mime",
				B_SYSTEM_SETTINGS_DIRECTORY)) {
			warning.ReplaceFirst("%target", B_TRANSLATE("the MIME settings"));
			requireOverride = false;
		} else if (DirectoryMatches(entry, B_USER_CONFIG_DIRECTORY)) {
			warning.ReplaceFirst("%target", B_TRANSLATE("the config folder"));
			requireOverride = false;
		} else if (DirectoryMatches(entry, B_USER_SETTINGS_DIRECTORY)
			|| DirectoryMatches(entry, B_SYSTEM_SETTINGS_DIRECTORY)) {
			warning.ReplaceFirst("%target", B_TRANSLATE("the settings folder"));
			requireOverride = false;
		} else {
			// It was not a special directory/file after all. Allow renaming.
			return true;
		}
	} else
		return true;

	if (dontAsk)
		return false;

	if (confirmedAlready && *confirmedAlready == kConfirmedHomeMove
		&& !requireOverride)
		// we already warned about moving home this time around
		return true;

	struct utsname name;
	if (uname(&name) == -1)
		warning.ReplaceFirst("%osName", "Haiku");
	else
		warning.ReplaceFirst("%osName", name.sysname);

	BString buttonLabel;
	if (action == kRename) {
		buttonLabel = B_TRANSLATE_COMMENT("Rename", "button label");
	} else if (action == kMove) {
		buttonLabel = B_TRANSLATE_COMMENT("Move", "button label");
	} else {
		buttonLabel = B_TRANSLATE_COMMENT("I know what I'm doing",
			"button label");
	}

	OverrideAlert* alert = new OverrideAlert("", warning.String(),
		buttonLabel.String(), (requireOverride ? B_SHIFT_KEY : 0),
		B_TRANSLATE("Cancel"), 0, NULL, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	if (alert->Go() == 1) {
		if (confirmedAlready)
			*confirmedAlready = kNotConfirmed;
		return false;
	}

	if (confirmedAlready) {
		if (!requireOverride)
			*confirmedAlready = kConfirmedHomeMove;
		else
			*confirmedAlready = kConfirmedAll;
	}

	return true;
}


status_t
EditModelName(const Model* model, const char* name, size_t length)
{
	if (model == NULL || name == NULL || name[0] == '\0' || length <= 0)
		return B_BAD_VALUE;

	BEntry entry(model->EntryRef());
	status_t result = entry.InitCheck();
	if (result != B_OK)
		return result;

	// TODO: use model-flavor specific virtuals for these special renamings

	if (model->HasLocalizedName() || model->IsDesktop() || model->IsRoot()
		|| model->IsTrash() || model->IsVirtualDirectory()) {
		result = B_NOT_ALLOWED;
	} else if (model->IsQuery()) {
		// write to query parameter
		BModelWriteOpener opener(const_cast<Model*>(model));
		ASSERT(model->Node());
		MoreOptionsStruct::SetQueryTemporary(model->Node(), false);

		RenameUndo undo(entry, name);
		result = entry.Rename(name);
		if (result != B_OK)
			undo.Remove();
	} else if (model->IsVolume()) {
		// write volume name
		BVolume volume(model->NodeRef()->device);
		result = volume.InitCheck();
		if (result == B_OK && volume.IsReadOnly())
			result = B_READ_ONLY_DEVICE;
		if (result == B_OK) {
			RenameVolumeUndo undo(volume, name);
			result = volume.SetName(name);
			if (result != B_OK)
				undo.Remove();
		}
	} else {
		BVolume volume(model->NodeRef()->device);
		result = volume.InitCheck();
		if (result == B_OK && volume.IsReadOnly())
			result = B_READ_ONLY_DEVICE;
		if (result == B_OK)
			result = ShouldEditRefName(model->EntryRef(), name, length);
		if (result == B_OK) {
			RenameUndo undo(entry, name);
			result = entry.Rename(name);
			if (result != B_OK)
				undo.Remove();
		}
	}

	return result;
}


status_t
ShouldEditRefName(const entry_ref* ref, const char* name, size_t length)
{
	if (ref == NULL || name == NULL || name[0] == '\0' || length <= 0)
		return B_BAD_VALUE;

	BEntry entry(ref);
	if (entry.InitCheck() != B_OK)
		return B_NO_INIT;

	// check if name is too long
	if (length >= B_FILE_NAME_LENGTH) {
		BString text;
		if (entry.IsDirectory())
			text = B_TRANSLATE("The entered folder name is too long.");
		else
			text = B_TRANSLATE("The entered file name is too long.");

		BAlert* alert = new BAlert("", text, B_TRANSLATE("OK"),
			0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();

		return B_NAME_TOO_LONG;
	}

	// same name
	if (strcmp(name, ref->name) == 0)
		return B_OK;

	// user declined rename in system directory
	if (!ConfirmChangeIfWellKnownDirectory(&entry, kRename))
		return B_CANCELED;

	// entry must have a parent directory
	BDirectory parent;
	if (entry.GetParent(&parent) != B_OK)
		return B_ERROR;

	// check for name conflict
	if (parent.Contains(name)) {
		BString text(B_TRANSLATE("An item named '%filename%' already exists."));
		text.ReplaceFirst("%filename%", name);

		BAlert* alert = new BAlert("", text, B_TRANSLATE("OK"),
			0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();

		return B_NAME_IN_USE;
	}

	// success
	return B_OK;
}


static status_t
InitCopy(CopyLoopControl* loopControl, uint32 moveMode,
	BObjectList<entry_ref, true>* srcList, BVolume* dstVol, BDirectory* destDir,
	entry_ref* destRef, bool preflightNameCheck, bool needSizeCalculation,
	int32* collisionCount, ConflictCheckResult* preflightResult)
{
	if (dstVol->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	int32 numItems = srcList->CountItems();
	int32 askOnceOnly = kNotConfirmed;
	for (int32 index = 0; index < numItems; index++) {
		// we could check for this while iterating through items in each of
		// the copy loops, except it takes forever to call CalcItemsAndSize
		BEntry entry((entry_ref*)srcList->ItemAt(index));
		if (FSIsRootDir(&entry)) {
			BString errorStr;
			if (moveMode == kCreateLink) {
				errorStr.SetTo(
					B_TRANSLATE("You cannot create a link to the root "
					"directory."));
			} else {
				errorStr.SetTo(
					B_TRANSLATE("You cannot copy or move the root "
					"directory."));
			}

			BAlert* alert = new BAlert("", errorStr.String(),
				B_TRANSLATE("Cancel"), 0, 0, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			return B_ERROR;
		}
		if (moveMode == kMoveSelectionTo
			&& !ConfirmChangeIfWellKnownDirectory(&entry, kMove,
				false, &askOnceOnly)) {
			return B_ERROR;
		}
	}

	if (preflightNameCheck) {
		ASSERT(collisionCount);
		ASSERT(preflightResult);

		*preflightResult = kPrompt;
		*collisionCount = 0;

		*preflightResult = PreFlightNameCheck(srcList, destDir,
			collisionCount, moveMode);
		if (*preflightResult == kCanceled) {
			// user canceled
			return B_ERROR;
		}
	}

	// set up the status display
	switch (moveMode) {
		case kCopySelectionTo:
		case kDuplicateSelection:
		case kMoveSelectionTo:
			{
				loopControl->Init(moveMode == kMoveSelectionTo ? kMoveState
					: kCopyState);

				int32 totalItems = 0;
				off_t totalSize = 0;
				if (needSizeCalculation) {
					if (CalcItemsAndSize(loopControl, srcList,
							dstVol->BlockSize(), &totalItems, &totalSize)
							!= B_OK) {
						return B_ERROR;
					}

					// check for free space before starting copy
					if ((totalSize + (4* kKBSize)) >= dstVol->FreeBytes()) {
						BAlert* alert = new BAlert("",
							B_TRANSLATE_NOCOLLECT(kNoFreeSpace),
							B_TRANSLATE("Cancel"),
							0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
						alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
						alert->Go();
						return B_ERROR;
					}
				}

				loopControl->Init(totalItems, totalSize, destRef);
				break;
			}

		case kCreateLink:
			if (numItems > 10) {
				// this will be fast, only put up status if lots of items
				// moved, links created
				loopControl->Init(kCreateLinkState);
				loopControl->Init(numItems, numItems, destRef);
			}
			break;
	}

	return B_OK;
}


// ToDo:
// get rid of this cruft
bool
delete_ref(void* ref)
{
	delete (entry_ref*)ref;
	return false;
}


bool
delete_point(void* point)
{
	delete (BPoint*)point;
	return false;
}


static status_t
MoveTask(BObjectList<entry_ref, true>* srcList, BEntry* destEntry, BList* pointList, uint32 moveMode)
{
	ASSERT(!srcList->IsEmpty());

	// extract information from src, dest models
	// ## note that we're assuming all items come from the same volume
	// ## by looking only at FirstItem here which is not a good idea
	dev_t srcVolumeDevice = srcList->FirstItem()->device;
	dev_t destVolumeDevice = srcVolumeDevice;

	StatStruct deststat;
	BVolume volume(srcVolumeDevice);
	entry_ref destRef;

	bool destIsTrash = false;
	BDirectory destDir;
	BDirectory* destDirToCheck = NULL;
	bool needPreflightNameCheck = false;
	bool sourceIsReadOnly = volume.IsReadOnly();
	volume.Unset();

	bool fromUndo = FSIsUndoMoveMode(moveMode);
	moveMode = FSMoveMode(moveMode);

	// if we're not passed a destEntry then we are supposed to move to trash
	if (destEntry != NULL) {
		destEntry->GetRef(&destRef);

		destDir.SetTo(destEntry);
		destDir.GetStat(&deststat);
		destDirToCheck = &destDir;

		destVolumeDevice = deststat.st_dev;
		destIsTrash = FSIsTrashDir(destEntry);
		volume.SetTo(destVolumeDevice);

		needPreflightNameCheck = true;
	} else if (moveMode == kDuplicateSelection) {
		BEntry entry;
		entry.SetTo(srcList->FirstItem());
		entry.GetParent(&destDir);
		volume.SetTo(srcVolumeDevice);
	} else {
		// move is to trash
		destIsTrash = true;

		FSGetTrashDir(&destDir, srcVolumeDevice);
		volume.SetTo(srcVolumeDevice);

		BEntry entry;
		destDir.GetEntry(&entry);
		destDirToCheck = &destDir;

		entry.GetRef(&destRef);
	}

	// change the move mode if needed
	if (moveMode == kCopySelectionTo && destIsTrash) {
		// cannot copy to trash
		moveMode = kMoveSelectionTo;
	}

	if (moveMode == kMoveSelectionTo && sourceIsReadOnly)
		moveMode = kCopySelectionTo;

	bool needSizeCalculation = true;
	if ((moveMode == kMoveSelectionTo && srcVolumeDevice == destVolumeDevice)
		|| destIsTrash) {
		needSizeCalculation = false;
	}

	// we need the undo object later on, so we create it no matter
	// if we really need it or not (it's very lightweight)
	MoveCopyUndo undo(srcList, destDir, pointList, moveMode);
	if (fromUndo)
		undo.Remove();

	TrackerCopyLoopControl loopControl;

	ConflictCheckResult conflictCheckResult = kPrompt;
	int32 collisionCount = 0;
	// TODO: Status item is created in InitCopy(), but it would be kind of
	// neat to move all that into TrackerCopyLoopControl
	status_t result = InitCopy(&loopControl, moveMode, srcList,
		&volume, destDirToCheck, &destRef, needPreflightNameCheck,
		needSizeCalculation, &collisionCount, &conflictCheckResult);

	loopControl.SetSourceList(srcList);

	if (result == B_OK) {
		for (int32 i = 0; i < srcList->CountItems(); i++) {
			BPoint* loc = (BPoint*)-1;
				// a loc of -1 forces autoplacement, rather than copying the
				// position of the original node
				// TODO:
				// Clean this mess up!
				// What could be a cleaner design is to pass along some kind
				// "filter" object that post-processes poses, i.e. adds the
				// location or other stuff. It should not be a job of the
				// copy-engine.

			entry_ref* srcRef = srcList->ItemAt(i);

			if (moveMode == kDuplicateSelection) {
				BEntry entry(srcRef);
				entry.GetParent(&destDir);
				destDir.GetStat(&deststat);
				volume.SetTo(srcRef->device);
			}

			// handle case where item is dropped into folder it already lives
			// in which could happen if dragging from a query window
			if (moveMode != kCreateLink
				&& moveMode != kCreateRelativeLink
				&& moveMode != kDuplicateSelection
				&& !destIsTrash
				&& (srcRef->device == destRef.device
				&& srcRef->directory == deststat.st_ino)) {
				continue;
			}

			if (loopControl.CheckUserCanceled())
				break;

			BEntry sourceEntry(srcRef);
			if (sourceEntry.InitCheck() != B_OK) {
				BString error(B_TRANSLATE("Error moving \"%name\"."));
				error.ReplaceFirst("%name", srcRef->name);
				BAlert* alert = new BAlert("", error.String(),
					B_TRANSLATE("Cancel"), 0, 0, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
				break;
			}

			// are we moving item to trash?
			if (destIsTrash) {
				if (pointList != NULL)
					loc = (BPoint*)pointList->ItemAt(i);

				result = MoveEntryToTrash(&sourceEntry, loc, undo);
				if (result != B_OK) {
					BString error(B_TRANSLATE("Error moving \"%name\" to Trash. "
						"(%error)"));
					error.ReplaceFirst("%name", srcRef->name);
					error.ReplaceFirst("%error", strerror(result));
					BAlert* alert = new BAlert("", error.String(),
						B_TRANSLATE("Cancel"), 0, 0, B_WIDTH_AS_USUAL,
						B_WARNING_ALERT);
					alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
					alert->Go();
					break;
				}
				continue;
			}

			// resolve name collisions and hierarchy problems
			if (CheckName(moveMode, &sourceEntry, &destDir,
					collisionCount > 1, conflictCheckResult) != B_OK) {
				// we will skip the current item, because we got a conflict
				// and were asked to or because there was some conflict

				// update the status because item got skipped and the status
				// will not get updated by the move call
				loopControl.UpdateStatus(srcRef->name, *srcRef, 1);

				continue;
			}

			// get location to place this item
			if (pointList != NULL && moveMode != kCopySelectionTo) {
				loc = (BPoint*)pointList->ItemAt(i);

				BNode* sourceNode = GetWritableNode(&sourceEntry);
				if (sourceNode && sourceNode->InitCheck() == B_OK) {
					PoseInfo poseInfo;
					poseInfo.fInvisible = false;
					poseInfo.fInitedDirectory = deststat.st_ino;
					poseInfo.fLocation = *loc;
					sourceNode->WriteAttr(kAttrPoseInfo, B_RAW_TYPE, 0, &poseInfo,
						sizeof(poseInfo));
				}
				delete sourceNode;
			}

			if (pointList != NULL)
				loc = (BPoint*)pointList->ItemAt(i);

			result = MoveItem(&sourceEntry, &destDir, loc, moveMode, NULL, undo, &loopControl);
			if (result != B_OK)
				break;
		}
	}

	// duplicates of srcList, destFolder were created - dispose them
	delete srcList;
	delete destEntry;

	// delete file location list and all Points within
	if (pointList != NULL) {
		pointList->DoForEach(delete_point);
		delete pointList;
	}

	return B_OK;
}


class FailWithAlert {
	public:
		static void FailOnError(status_t error, const char* string,
			const char* name = NULL)
		{
			if (error != B_OK)
				throw FailWithAlert(error, string, name);
		}

		FailWithAlert(status_t error, const char* string, const char* name)
			:
			fString(string),
			fName(name),
			fError(error)
		{
		}

		const char* fString;
		const char* fName;
		status_t fError;
};


class MoveError {
public:
	static void FailOnError(status_t error)
	{
		if (error != B_OK)
			throw MoveError(error);
	}

	MoveError(status_t error)
		:
		fError(error)
	{
	}

	status_t fError;
};


void
CopyFile(BEntry* srcFile, StatStruct* srcStat, BDirectory* destDir,
	CopyLoopControl* loopControl, BPoint* loc, bool makeOriginalName,
	Undo &undo)
{
	if (loopControl->SkipEntry(srcFile, true))
		return;

	node_ref node;
	destDir->GetNodeRef(&node);
	BVolume volume(node.device);

	// check for free space first
	if ((srcStat->st_size + kKBSize) >= volume.FreeBytes()) {
		loopControl->FileError(B_TRANSLATE_NOCOLLECT(kNoFreeSpace), "",
			B_DEVICE_FULL, false);
		throw (status_t)B_DEVICE_FULL;
	}

	char destName[B_FILE_NAME_LENGTH];
	srcFile->GetName(destName);
	entry_ref ref;
	srcFile->GetRef(&ref);

	loopControl->UpdateStatus(destName, ref, 1024, true);

	if (makeOriginalName) {
		BString suffix(" ");
		suffix << B_TRANSLATE_COMMENT("copy", "filename copy"),
		FSMakeOriginalName(destName, destDir, suffix.String());
		undo.UpdateEntry(srcFile, destName);
	}

	BEntry conflictingEntry;
	if (destDir->FindEntry(destName, &conflictingEntry) == B_OK) {
		switch (loopControl->OverwriteOnConflict(srcFile, destName, destDir,
				false, false)) {
			case TrackerCopyLoopControl::kSkip:
				// we are about to ignore this entire directory
				return;

			case TrackerCopyLoopControl::kReplace:
				if (!conflictingEntry.IsDirectory()) {
					ThrowOnError(conflictingEntry.Remove());
					break;
				}
				// fall through if not a directory
			case TrackerCopyLoopControl::kMerge:
				// This flag implies that the attributes should be kept
				// on the file.  Just ignore it.
				break;
		}
	}

	try {
		LowLevelCopy(srcFile, srcStat, destDir, destName, loopControl, loc);
	} catch (status_t err) {
		if (err == kCopyCanceled)
			throw (status_t)err;

		if (err != B_OK) {
			if (!loopControl->FileError(
					B_TRANSLATE_NOCOLLECT(kFileErrorString), destName, err,
					true)) {
				throw (status_t)err;
			} else {
				// user selected continue in spite of error, update status bar
				loopControl->UpdateStatus(NULL, ref, (int32)srcStat->st_size);
			}
		}
	}
}


#ifdef _SILENTLY_CORRECT_FILE_NAMES
static bool
CreateFileSystemCompatibleName(const BDirectory* destDir, char* destName)
{
	// Is it a FAT32 file system?
	// (this is the only one we currently know about)

	BEntry target;
	destDir->GetEntry(&target);
	entry_ref targetRef;
	fs_info info;
	if (target.GetRef(&targetRef) == B_OK
		&& fs_stat_dev(targetRef.device, &info) == B_OK
		&& !strcmp(info.fsh_name, "fat")) {
		bool wasInvalid = false;

		// it's a FAT32 file system, now check the name

		int32 length = strlen(destName) - 1;
		while (destName[length] == '.') {
			// invalid name, just cut off the dot at the end
			destName[length--] = '\0';
			wasInvalid = true;
		}

		char* invalid = destName;
		while ((invalid = strpbrk(invalid, "?<>\\:\"|*")) != NULL) {
			invalid[0] = '_';
			wasInvalid = true;
		}

		return wasInvalid;
	}

	return false;
}
#endif


static void
LowLevelCopy(BEntry* srcEntry, StatStruct* srcStat, BDirectory* destDir,
	char* destName, CopyLoopControl* loopControl, BPoint* loc)
{
	entry_ref ref;
	ThrowOnError(srcEntry->GetRef(&ref));

	if (S_ISLNK(srcStat->st_mode)) {
		// handle symbolic links
		BSymLink srcLink;
		BSymLink newLink;
		char linkpath[MAXPATHLEN];

		ThrowOnError(srcLink.SetTo(srcEntry));
		ssize_t size = srcLink.ReadLink(linkpath, MAXPATHLEN - 1);
		if (size < 0)
			ThrowOnError(size);
		ThrowOnError(destDir->CreateSymLink(destName, linkpath, &newLink));

		node_ref destNodeRef;
		destDir->GetNodeRef(&destNodeRef);
		// copy or write new pose location as a first thing
		SetupPoseLocation(ref.directory, destNodeRef.node, &srcLink,
			&newLink, loc);

		BNodeInfo nodeInfo(&newLink);
		nodeInfo.SetType(B_LINK_MIMETYPE);

		newLink.SetPermissions(srcStat->st_mode);
		newLink.SetOwner(srcStat->st_uid);
		newLink.SetGroup(srcStat->st_gid);
		newLink.SetModificationTime(srcStat->st_mtime);
		newLink.SetCreationTime(srcStat->st_crtime);

		return;
	}

	BFile srcFile(srcEntry, O_RDONLY);
	ThrowOnInitCheckError(&srcFile);

	const size_t kMinBufferSize = 1024* 128;
	const size_t kMaxBufferSize = 1024* 1024;

	size_t bufsize = kMinBufferSize;
	if ((off_t)bufsize < srcStat->st_size) {
		// File bigger than the buffer size: determine an optimal buffer size
		system_info sinfo;
		get_system_info(&sinfo);
		size_t freesize = static_cast<size_t>(
			(sinfo.max_pages - sinfo.used_pages) * B_PAGE_SIZE);
		bufsize = freesize / 4;
			// take 1/4 of RAM max
		bufsize -= bufsize % (16* 1024);
			// Round to 16 KB boundaries
		if (bufsize < kMinBufferSize) {
			// at least kMinBufferSize
			bufsize = kMinBufferSize;
		} else if (bufsize > kMaxBufferSize) {
			// no more than kMaxBufferSize
			bufsize = kMaxBufferSize;
		}
	}

	BFile destFile(destDir, destName, O_RDWR | O_CREAT);
#ifdef _SILENTLY_CORRECT_FILE_NAMES
	if ((destFile.InitCheck() == B_BAD_VALUE
		|| destFile.InitCheck() == B_NOT_ALLOWED)
		&& CreateFileSystemCompatibleName(destDir, destName)) {
		destFile.SetTo(destDir, destName, B_CREATE_FILE | B_READ_WRITE);
	}
#endif

	ThrowOnInitCheckError(&destFile);

	node_ref destNodeRef;
	destDir->GetNodeRef(&destNodeRef);
	// copy or write new pose location as a first thing
	SetupPoseLocation(ref.directory, destNodeRef.node, &srcFile,
		&destFile, loc);

	char* buffer = new char[bufsize];
	try {
		// copy data portion of file
		while (true) {
			if (loopControl->CheckUserCanceled()) {
				// if copy was canceled, remove partial destination file
				destFile.Unset();

				BEntry destEntry;
				if (destDir->FindEntry(destName, &destEntry) == B_OK)
					destEntry.Remove();

				throw (status_t)kCopyCanceled;
			}

			ASSERT(buffer);
			ssize_t bytes = srcFile.Read(buffer, bufsize);

			if (bytes > 0) {
				ssize_t updateBytes = 0;
				if (bytes > 32* 1024) {
					// when copying large chunks, update after read and after
					// write to get better update granularity
					updateBytes = bytes / 2;
					loopControl->UpdateStatus(NULL, ref, updateBytes, true);
				}

				loopControl->ChecksumChunk(buffer, (size_t)bytes);

				ssize_t result = destFile.Write(buffer, (size_t)bytes);
				if (result != bytes) {
					if (result < 0)
						throw (status_t)result;
					throw (status_t)B_ERROR;
				}

				result = destFile.Sync();
				if (result != B_OK)
					throw (status_t)result;

				loopControl->UpdateStatus(NULL, ref, bytes - updateBytes,
					true);
			} else if (bytes < 0) {
				// read error
				throw (status_t)bytes;
			} else {
				// we are done
				break;
			}
		}

		CopyAttributes(loopControl, &srcFile, &destFile, buffer, bufsize);
	} catch (...) {
		delete[] buffer;
		throw;
	}

	destFile.SetPermissions(srcStat->st_mode);
	destFile.SetOwner(srcStat->st_uid);
	destFile.SetGroup(srcStat->st_gid);
	destFile.SetModificationTime(srcStat->st_mtime);
	destFile.SetCreationTime(srcStat->st_crtime);

	delete[] buffer;

	if (!loopControl->ChecksumFile(&ref)) {
		// File no good.  Remove and quit.
		destFile.Unset();

		BEntry destEntry;
		if (destDir->FindEntry(destName, &destEntry) == B_OK)
			destEntry.Remove();
		throw (status_t)kUserCanceled;
	}
}


void
CopyAttributes(CopyLoopControl* control, BNode* srcNode, BNode* destNode,
	void* buffer, size_t bufsize)
{
	// ToDo:
	// Add error checking
	// prior to coyping attributes, make sure indices are installed

	// When calling CopyAttributes on files, have to make sure destNode
	// is a BFile opened R/W

	srcNode->RewindAttrs();
	char name[256];
	while (srcNode->GetNextAttrName(name) == B_OK) {
		// Check to see if this attribute should be skipped.
		if (control->SkipAttribute(name))
			continue;

		attr_info info;
		if (srcNode->GetAttrInfo(name, &info) != B_OK)
			continue;

		// Check to see if this attribute should be overwritten when it
		// already exists.
		if (control->PreserveAttribute(name)) {
			attr_info dest_info;
			if (destNode->GetAttrInfo(name, &dest_info) == B_OK)
				continue;
		}

		// Special case for a size 0 attribute. It wouldn't be written at all
		// otherwise.
		if (info.size == 0)
			destNode->WriteAttr(name, info.type, 0, buffer, 0);

		ssize_t bytes;
		ssize_t numToRead = (ssize_t)info.size;
		for (off_t offset = 0; numToRead > 0; offset += bytes) {
			size_t chunkSize = (size_t)numToRead;
			if (chunkSize > bufsize)
				chunkSize = bufsize;

			bytes = srcNode->ReadAttr(name, info.type, offset,
				buffer, chunkSize);

			if (bytes <= 0)
				break;

			destNode->WriteAttr(name, info.type, offset, buffer,
				(size_t)bytes);

			numToRead -= bytes;
		}
	}
}


static void
CopyFolder(BEntry* srcEntry, BDirectory* destDir,
	CopyLoopControl* loopControl, BPoint* loc, bool makeOriginalName,
	Undo &undo, bool removeSource = false)
{
	BDirectory newDir;
	BEntry entry;
	status_t err = B_OK;
	bool createDirectory = true;
	BEntry existingEntry;

	if (loopControl->SkipEntry(srcEntry, false))
		return;

	entry_ref ref;
	srcEntry->GetRef(&ref);

	char destName[B_FILE_NAME_LENGTH];
	strlcpy(destName, ref.name, sizeof(destName));

	loopControl->UpdateStatus(ref.name, ref, 1024, true);

	if (makeOriginalName) {
		BString suffix(" ");
		suffix << B_TRANSLATE_COMMENT("copy", "filename copy"),
		FSMakeOriginalName(destName, destDir, suffix.String());
		undo.UpdateEntry(srcEntry, destName);
	}

	if (destDir->FindEntry(destName, &existingEntry) == B_OK) {
		// some entry with a conflicting name is already present in destDir
		// decide what to do about it
		bool isDirectory = existingEntry.IsDirectory();

		switch (loopControl->OverwriteOnConflict(srcEntry, destName, destDir,
			true, isDirectory)) {
			case TrackerCopyLoopControl::kSkip:
				// we are about to ignore this entire directory
				return;


			case TrackerCopyLoopControl::kReplace:
				if (!isDirectory) {
					// conflicting with a file or symbolic link, remove entry
					ThrowOnError(existingEntry.Remove());
					break;
				}
			// fall through if directory, do not replace.
			case TrackerCopyLoopControl::kMerge:
				ASSERT(isDirectory);
				// do not create a new directory, use the current one
				newDir.SetTo(&existingEntry);
				createDirectory = false;
				break;
		}
	}

	// loop through everything in src folder and copy it to new folder
	BDirectory srcDir(srcEntry);
	srcDir.Rewind();

	// create a new folder inside of destination folder
	if (createDirectory) {
	 	err = destDir->CreateDirectory(destName, &newDir);
#ifdef _SILENTLY_CORRECT_FILE_NAMES
	 	if (err == B_BAD_VALUE) {
	 		// check if it's an invalid name on a FAT32 file system
	 		if (CreateFileSystemCompatibleName(destDir, destName))
	 			err = destDir->CreateDirectory(destName, &newDir);
	 	}
#endif
		if (err != B_OK) {
			if (!loopControl->FileError(B_TRANSLATE_NOCOLLECT(
					kFolderErrorString), destName, err, true)) {
				throw err;
			}

			// will allow rest of copy to continue
			return;
		}
	}

	char* buffer;
	if (createDirectory && err == B_OK
		&& (buffer = (char*)malloc(32768)) != 0) {
		CopyAttributes(loopControl, &srcDir, &newDir, buffer, 32768);
			// don't copy original pose location if new location passed
		free(buffer);
	}

	StatStruct statbuf;
	srcDir.GetStat(&statbuf);
	dev_t sourceDeviceID = statbuf.st_dev;

	// copy or write new pose location
	node_ref destNodeRef;
	destDir->GetNodeRef(&destNodeRef);
	SetupPoseLocation(ref.directory, destNodeRef.node, &srcDir,
		&newDir, loc);

	while (srcDir.GetNextEntry(&entry) == B_OK) {

		if (loopControl->CheckUserCanceled())
			throw (status_t)kUserCanceled;

		entry.GetStat(&statbuf);

		if (S_ISDIR(statbuf.st_mode)) {

			// entry is a mount point, do not copy it
			if (statbuf.st_dev != sourceDeviceID) {
				PRINT(("Avoiding mount point %" B_PRIdDEV ", %" B_PRIdDEV "\n",
					statbuf.st_dev, sourceDeviceID));
				continue;
			}

			CopyFolder(&entry, &newDir, loopControl, 0, false, undo,
				removeSource);
			if (removeSource)
				FSDeleteFolder(&entry, loopControl, true, true, false);
		} else if (S_ISREG(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)) {
			CopyFile(&entry, &statbuf, &newDir, loopControl, 0, false, undo);
			if (removeSource)
				entry.Remove();
		} else {
			// Ignore special files
		}
	}
	if (removeSource)
		srcEntry->Remove();
	else
		srcEntry->Unset();
}


status_t
RecursiveMove(BEntry* entry, BDirectory* destDir, CopyLoopControl* loopControl)
{
	const char* name = entry->Name();

	if (destDir->Contains(name)) {
		BPath path (destDir, name);
		BDirectory subDir (path.Path());
		entry_ref ref;
		entry->GetRef(&ref);
		BDirectory source(&ref);
		if (source.InitCheck() == B_OK) {
			source.Rewind();
			BEntry current;
			while (source.GetNextEntry(&current) == B_OK) {
				if (current.IsDirectory()) {
					RecursiveMove(&current, &subDir, loopControl);
					current.Remove();
				} else {
					name = current.Name();
					if (loopControl->OverwriteOnConflict(&current, name,
							&subDir, true, false)
								!= TrackerCopyLoopControl::kSkip) {
						MoveError::FailOnError(current.MoveTo(&subDir,
							NULL, true));
					}
				}
			}
		}
		entry->Remove();
	} else
		MoveError::FailOnError(entry->MoveTo(destDir));

	return B_OK;
}

status_t
MoveItem(BEntry* entry, BDirectory* destDir, BPoint* loc, uint32 moveMode,
	const char* newName, Undo &undo, CopyLoopControl* loopControl)
{
	entry_ref ref;
	try {
		node_ref destNode;
		StatStruct statbuf;
		MoveError::FailOnError(entry->GetStat(&statbuf));
		MoveError::FailOnError(entry->GetRef(&ref));
		MoveError::FailOnError(destDir->GetNodeRef(&destNode));

		if (moveMode == kCreateLink || moveMode == kCreateRelativeLink) {
			PoseInfo poseInfo;
			char name[B_FILE_NAME_LENGTH];
			strlcpy(name, ref.name, sizeof(name));

			BSymLink link;
			BString suffix(" ");
			suffix << B_TRANSLATE_COMMENT("link", "filename link"),
			FSMakeOriginalName(name, destDir, suffix.String());
			undo.UpdateEntry(entry, name);

			BPath path;
			entry->GetPath(&path);
			if (loc && loc != (BPoint*)-1) {
				poseInfo.fInvisible = false;
				poseInfo.fInitedDirectory = destNode.node;
				poseInfo.fLocation = *loc;
			}

			status_t err = B_ERROR;

			if (moveMode == kCreateRelativeLink) {
				if (statbuf.st_dev == destNode.device) {
					// relative link only works on the same device
					char oldwd[B_PATH_NAME_LENGTH];
					getcwd(oldwd, B_PATH_NAME_LENGTH);

					BEntry destEntry;
					destDir->GetEntry(&destEntry);
					BPath destPath;
					destEntry.GetPath(&destPath);

					chdir(destPath.Path());
						// change working dir to target dir

					BString destString(destPath.Path());
					destString.Append("/");

					BString srcString(path.Path());
					srcString.RemoveLast(path.Leaf());

					// find index while paths are the same

					const char* src = srcString.String();
					const char* dest = destString.String();
					const char* lastFolderSrc = src;
					const char* lastFolderDest = dest;

					while (*src && *dest && *src == *dest) {
						++src;
						if (*dest++ == '/') {
							lastFolderSrc = src;
							lastFolderDest = dest;
						}
					}
					src = lastFolderSrc;
					dest = lastFolderDest;

					BString source;
					if (*dest == '\0' && *src != '\0') {
						// source is deeper in the same tree than the target
						source.Append(src);
					} else if (*dest != '\0') {
						// target is deeper in the same tree than the source
						while (*dest) {
							if (*dest == '/')
								source.Prepend("../");
							++dest;
						}
						source.Append(src);
					}

					// else source and target are in the same dir

					source.Append(path.Leaf());
					err = destDir->CreateSymLink(name, source.String(),
						&link);

					chdir(oldwd);
						// change working dir back to original
				} else
					moveMode = kCreateLink;
						// fall back to absolute link mode
			}

			if (moveMode == kCreateLink)
				err = destDir->CreateSymLink(name, path.Path(), &link);

			if (err == B_UNSUPPORTED) {
				throw FailWithAlert(err,
					B_TRANSLATE("The target disk does not support "
					"creating links."), NULL);
			}

			FailWithAlert::FailOnError(err,
				B_TRANSLATE("Error creating link to \"%name\"."),
				ref.name);

			if (loc && loc != (BPoint*)-1) {
				link.WriteAttr(kAttrPoseInfo, B_RAW_TYPE, 0, &poseInfo,
					sizeof(PoseInfo));
			}

			BNodeInfo nodeInfo(&link);
			nodeInfo.SetType(B_LINK_MIMETYPE);
			return B_OK;
		}

		// if move is on same volume don't copy
		if (statbuf.st_dev == destNode.device && moveMode != kCopySelectionTo
			&& moveMode != kDuplicateSelection) {
			// for "Move" the size for status is always 1 - since file
			// size is irrelevant when simply moving to a new folder
			loopControl->UpdateStatus(ref.name, ref, 1);
			if (entry->IsDirectory())
				return RecursiveMove(entry, destDir, loopControl);

			MoveError::FailOnError(entry->MoveTo(destDir, newName));
		} else {
			bool makeOriginalName = (moveMode == kDuplicateSelection);
			if (S_ISDIR(statbuf.st_mode)) {
				CopyFolder(entry, destDir, loopControl, loc, makeOriginalName,
					undo, moveMode == kMoveSelectionTo);
			} else {
				CopyFile(entry, &statbuf, destDir, loopControl, loc,
					makeOriginalName, undo);
				if (moveMode == kMoveSelectionTo)
					entry->Remove();
			}
		}
	} catch (status_t error) {
		// no alert, was already taken care of before
		return error;
	} catch (MoveError& error) {
		BString errorString(B_TRANSLATE("Error moving \"%name\""));
		errorString.ReplaceFirst("%name", ref.name);
		BAlert* alert = new BAlert("", errorString.String(), B_TRANSLATE("OK"),
			0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
		return error.fError;
	} catch (FailWithAlert& error) {
		BString buffer(error.fString);
		if (error.fName != NULL)
			buffer.ReplaceFirst("%name", error.fName);
		else
			buffer << error.fString;

		BAlert* alert = new BAlert("", buffer.String(), B_TRANSLATE("OK"),
			0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();

		return error.fError;
	}

	return B_OK;
}


void
FSDuplicate(BObjectList<entry_ref, true>* srcList, BList* pointList)
{
	LaunchInNewThread("DupTask", B_NORMAL_PRIORITY, MoveTask, srcList,
		(BEntry*)NULL, pointList, kDuplicateSelection);
}


#if 0
status_t
FSCopyFolder(BEntry* srcEntry, BDirectory* destDir,
	CopyLoopControl* loopControl, BPoint* loc, bool makeOriginalName)
{
	try
		CopyFolder(srcEntry, destDir, loopControl, loc, makeOriginalName);
	catch (status_t error) {
		return error;

	return B_OK;
}
#endif


status_t
FSCopyAttributesAndStats(BNode* srcNode, BNode* destNode, bool copyTimes)
{
	char* buffer = new char[1024];

	// copy the attributes
	srcNode->RewindAttrs();
	char name[256];
	while (srcNode->GetNextAttrName(name) == B_OK) {
		attr_info info;
		if (srcNode->GetAttrInfo(name, &info) != B_OK)
			continue;

		attr_info dest_info;
		if (destNode->GetAttrInfo(name, &dest_info) == B_OK)
			continue;

		ssize_t bytes;
		ssize_t numToRead = (ssize_t)info.size;
		for (off_t offset = 0; numToRead > 0; offset += bytes) {
			size_t chunkSize = (size_t)numToRead;
			if (chunkSize > 1024)
				chunkSize = 1024;

			bytes = srcNode->ReadAttr(name, info.type, offset, buffer,
				chunkSize);

			if (bytes <= 0)
				break;

			destNode->WriteAttr(name, info.type, offset, buffer,
				(size_t)bytes);

			numToRead -= bytes;
		}
	}
	delete[] buffer;

	// copy the file stats
	struct stat srcStat;
	srcNode->GetStat(&srcStat);
	destNode->SetPermissions(srcStat.st_mode);
	destNode->SetOwner(srcStat.st_uid);
	destNode->SetGroup(srcStat.st_gid);
	if (copyTimes) {
		destNode->SetModificationTime(srcStat.st_mtime);
		destNode->SetCreationTime(srcStat.st_crtime);
	}

	return B_OK;
}


#if 0
status_t
FSCopyFile(BEntry* srcFile, StatStruct* srcStat, BDirectory* destDir,
	CopyLoopControl* loopControl, BPoint* loc, bool makeOriginalName)
{
	try {
		CopyFile(srcFile, srcStat, destDir, loopControl, loc,
			makeOriginalName);
	} catch (status_t error) {
		return error;
	}

	return B_OK;
}
#endif


static status_t
MoveEntryToTrash(BEntry* entry, BPoint* loc, Undo &undo)
{
	BDirectory trash_dir;
	entry_ref ref;
	status_t result = entry->GetRef(&ref);
	if (result != B_OK)
		return result;

	node_ref nodeRef;
	result = entry->GetNodeRef(&nodeRef);
	if (result != B_OK)
		return result;

	StatStruct statbuf;
	result = entry->GetStat(&statbuf);
	if (entry->GetStat(&statbuf) != B_OK)
		return result;

	// if it's a directory close the window and any child dir windows
	if (S_ISDIR(statbuf.st_mode)) {
		BDirectory dir(entry);

		// if it's a volume, try to unmount
		if (dir.IsRootDirectory()) {
			BVolume volume(nodeRef.device);
			BVolume boot;

			BVolumeRoster().GetBootVolume(&boot);
			if (volume == boot) {
				char name[B_FILE_NAME_LENGTH];
				volume.GetName(name);
				BString buffer(
					B_TRANSLATE("Cannot unmount the boot volume \"%name\"."));
				buffer.ReplaceFirst("%name", name);
				BAlert* alert = new BAlert("", buffer.String(),
					B_TRANSLATE("Cancel"), 0, 0, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
			} else {
				BMessage message(kUnmountVolume);
				message.AddInt32("device_id", volume.Device());
				be_app->PostMessage(&message);
			}
			return B_OK;
		}

		// get trash directory on same volume as item being moved
		result = FSGetTrashDir(&trash_dir, nodeRef.device);
		if (result != B_OK)
			return result;

		// check hierarchy before moving
		BEntry trashEntry;
		trash_dir.GetEntry(&trashEntry);

		if (dir == trash_dir || dir.Contains(&trashEntry)) {
			BAlert* alert = new BAlert("",
				B_TRANSLATE("You cannot put the selected item(s) "
					"into the trash."),
				B_TRANSLATE("OK"),
				0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();

			// return no error so we don't get two dialogs
			return B_OK;
		}

		BMessage message(kCloseWindowAndChildren);

		node_ref parentNode;
		parentNode.device = statbuf.st_dev;
		parentNode.node = statbuf.st_ino;
		message.AddData("node_ref", B_RAW_TYPE, &parentNode, sizeof(node_ref));
		be_app->PostMessage(&message);
	} else {
		// get trash directory on same volume as item being moved
		result = FSGetTrashDir(&trash_dir, nodeRef.device);
		if (result != B_OK)
			return result;
	}

	// make sure name doesn't conflict with anything in trash already
	char name[B_FILE_NAME_LENGTH];
	strlcpy(name, ref.name, sizeof(name));
	if (trash_dir.Contains(name)) {
		BString suffix(" ");
		suffix << B_TRANSLATE_COMMENT("copy", "filename copy"),
		FSMakeOriginalName(name, &trash_dir, suffix.String());
		undo.UpdateEntry(entry, name);
	}

	BNode* sourceNode = 0;
	if (loc && loc != (BPoint*)-1 && (sourceNode = GetWritableNode(entry, &statbuf)) != 0) {
		trash_dir.GetStat(&statbuf);
		PoseInfo poseInfo;
		poseInfo.fInvisible = false;
		poseInfo.fInitedDirectory = statbuf.st_ino;
		poseInfo.fLocation = *loc;
		sourceNode->WriteAttr(kAttrPoseInfo, B_RAW_TYPE, 0, &poseInfo, sizeof(poseInfo));
		delete sourceNode;
	}

	BNode node(entry);
	BPath path;
	// Get path of entry before it's moved to the trash
	// and write it to the file as an attribute
	if (node.InitCheck() == B_OK && entry->GetPath(&path) == B_OK) {
		BString originalPath(path.Path());
		node.WriteAttrString(kAttrOriginalPath, &originalPath);
	}

	TrackerCopyLoopControl loopControl;
	MoveItem(entry, &trash_dir, loc, kMoveSelectionTo, name, undo, &loopControl);

	return B_OK;
}


ConflictCheckResult
PreFlightNameCheck(BObjectList<entry_ref, true>* srcList, const BDirectory* destDir,
	int32* collisionCount, uint32 moveMode)
{
	// count the number of name collisions in dest folder
	*collisionCount = 0;

	int32 count = srcList->CountItems();
	for (int32 i = 0; i < count; i++) {
		entry_ref* srcRef = srcList->ItemAt(i);
		BEntry entry(srcRef);
		BDirectory parent;
		entry.GetParent(&parent);

		if (parent != *destDir && destDir->Contains(srcRef->name))
			(*collisionCount)++;
	}

	// prompt user only if there is more than one collision, otherwise the
	// single collision case will be handled as a "Prompt" case by CheckName
	if (*collisionCount > 1) {
		const char* verb = (moveMode == kMoveSelectionTo)
			? B_TRANSLATE("moving") : B_TRANSLATE("copying");
		BString replaceMsg(B_TRANSLATE_NOCOLLECT(kReplaceManyStr));
		replaceMsg.ReplaceAll("%verb", verb);

		BAlert* alert = new BAlert();
		alert->SetText(replaceMsg.String());
		alert->AddButton(B_TRANSLATE("Cancel"));
		alert->AddButton(B_TRANSLATE("Prompt"));
		alert->AddButton(B_TRANSLATE("Skip all"));
		alert->AddButton(B_TRANSLATE("Replace all"));
		alert->SetShortcut(0, B_ESCAPE);
		switch (alert->Go()) {
			case 0:
				return kCanceled;

			case 1:
				// user selected "Prompt"
				return kPrompt;

			case 2:
				// user selected "Skip all"
				return kSkipAll;

			case 3:
				// user selected "Replace all"
				return kReplaceAll;
		}
	}

	return kNoConflicts;
}


void
FileStatToString(StatStruct* stat, char* buffer, int32 length)
{
	tm timeData;
	localtime_r(&stat->st_mtime, &timeData);

	BString size;
	static BStringFormat format(
		B_TRANSLATE("{0, plural, one{# byte} other{# bytes}}"));
	format.Format(size, stat->st_size);
	uint32 pos = snprintf(buffer, length, "\n\t(%s ", size.String());

	strftime(buffer + pos, length - pos, "%b %d %Y, %I:%M:%S %p)", &timeData);
}


status_t
CheckName(uint32 moveMode, const BEntry* sourceEntry,
	const BDirectory* destDir, bool multipleCollisions,
	ConflictCheckResult& conflictResolution)
{
	if (moveMode == kDuplicateSelection) {
		// when duplicating, we will never have a conflict
		return B_OK;
	}

	// see if item already exists in destination dir
	const char* name = sourceEntry->Name();
	bool sourceIsDirectory = sourceEntry->IsDirectory();

	BDirectory srcDirectory;
	if (sourceIsDirectory) {
		srcDirectory.SetTo(sourceEntry);
		BEntry destEntry;
		destDir->GetEntry(&destEntry);

		if (moveMode != kCreateLink && moveMode != kCreateRelativeLink
			&& (srcDirectory == *destDir
				|| srcDirectory.Contains(&destEntry))) {
			BAlert* alert = new BAlert("",
				B_TRANSLATE("You can't move a folder into itself "
				"or any of its own sub-folders."), B_TRANSLATE("OK"),
				0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			return B_ERROR;
		}
	}

	if (FSIsTrashDir(sourceEntry) && moveMode != kCreateLink
		&& moveMode != kCreateRelativeLink) {
		BAlert* alert = new BAlert("",
			B_TRANSLATE("You can't move or copy the trash."),
			B_TRANSLATE("OK"), 0, 0, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
		return B_ERROR;
	}

	BEntry entry;
	if (destDir->FindEntry(name, &entry) != B_OK) {
		// no conflict, return
		return B_OK;
	}

	if (moveMode == kCreateLink || moveMode == kCreateRelativeLink) {
		// if we are creating link in the same directory, the conflict will
		// be handled later by giving the link a unique name
		sourceEntry->GetParent(&srcDirectory);

		if (srcDirectory == *destDir)
			return B_OK;
	}

	bool destIsDir = entry.IsDirectory();
	// be sure not to replace the parent directory of the item being moved
	if (destIsDir) {
		BDirectory targetDir(&entry);
		if (targetDir.Contains(sourceEntry)) {
			BAlert* alert = new BAlert("",
				B_TRANSLATE("You can't replace a folder "
				"with one of its sub-folders."),
				B_TRANSLATE("OK"), 0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			return B_ERROR;
		}
	}

	// ensure that the user isn't trying to replace a file with folder
	// or vice-versa
	if (moveMode != kCreateLink
		&& moveMode != kCreateRelativeLink
		&& destIsDir != sourceIsDirectory) {
		BAlert* alert = new BAlert("", sourceIsDirectory
			? B_TRANSLATE("You cannot replace a file with a folder or a "
				"symbolic link.")
			: B_TRANSLATE("You cannot replace a folder or a symbolic link "
				"with a file."), B_TRANSLATE("OK"), 0, 0, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
		return B_ERROR;
	}

	if (conflictResolution == kSkipAll)
		return B_ERROR;

	if (conflictResolution != kReplaceAll) {
		// prompt user to determine whether to replace or not
		BString replaceMsg;

		if (moveMode == kCreateLink || moveMode == kCreateRelativeLink) {
			replaceMsg.SetTo(B_TRANSLATE_NOCOLLECT(kSymLinkReplaceStr));
			replaceMsg.ReplaceFirst("%name", name);
		} else if (sourceEntry->IsDirectory()) {
			replaceMsg.SetTo(B_TRANSLATE_NOCOLLECT(kDirectoryReplaceStr));
			replaceMsg.ReplaceFirst("%name", name);
			replaceMsg.ReplaceFirst("%verb",
				moveMode == kMoveSelectionTo
				? B_TRANSLATE("moving")
				: B_TRANSLATE("copying"));
		} else {
			char sourceBuffer[96], destBuffer[96];
			StatStruct statBuffer;

			if (!sourceEntry->IsDirectory()
				&& sourceEntry->GetStat(&statBuffer) == B_OK) {
				FileStatToString(&statBuffer, sourceBuffer, 96);
			} else
				sourceBuffer[0] = '\0';

			if (!entry.IsDirectory() && entry.GetStat(&statBuffer) == B_OK)
				FileStatToString(&statBuffer, destBuffer, 96);
			else
				destBuffer[0] = '\0';

			replaceMsg.SetTo(B_TRANSLATE_NOCOLLECT(kReplaceStr));
			replaceMsg.ReplaceAll("%name", name);
			replaceMsg.ReplaceFirst("%dest", destBuffer);
			replaceMsg.ReplaceFirst("%src", sourceBuffer);
			replaceMsg.ReplaceFirst("%movemode", moveMode == kMoveSelectionTo
				? B_TRANSLATE("moving") : B_TRANSLATE("copying"));
		}

		// special case single collision (don't need Replace All shortcut)
		BAlert* alert;
		if (multipleCollisions || sourceIsDirectory) {
			alert = new BAlert();
			alert->SetText(replaceMsg.String());
			alert->AddButton(B_TRANSLATE("Skip"));
			alert->AddButton(B_TRANSLATE("Skip all"));
			alert->AddButton(B_TRANSLATE("Replace"));
			alert->AddButton(B_TRANSLATE("Replace all"));
			switch (alert->Go()) {
				case 0:
					conflictResolution = kCanceled;
					return B_ERROR;
				case 1:
					conflictResolution = kSkipAll;
					return B_ERROR;
				case 2:
					conflictResolution = kReplace;
					break;
				case 3:
					conflictResolution = kReplaceAll;
					break;
			}
		} else {
			alert = new BAlert("", replaceMsg.String(),
				B_TRANSLATE("Cancel"), B_TRANSLATE("Replace"));
			alert->SetShortcut(0, B_ESCAPE);
			switch (alert->Go()) {
				case 0:
					conflictResolution = kCanceled;
					return B_ERROR;
				case 1:
					conflictResolution = kReplace;
					break;
			}
		}
	}

	// delete destination item
	if (destIsDir)
		return B_OK;

	status_t status = entry.Remove();
	if (status != B_OK) {
		BString error(B_TRANSLATE("There was a problem trying to replace "
			"\"%name\". The item might be open or busy."));
		error.ReplaceFirst("%name", name);
		BAlert* alert = new BAlert("", error.String(),
			B_TRANSLATE("Cancel"), 0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}

	return status;
}


status_t
FSDeleteFolder(BEntry* dirEntry, CopyLoopControl* loopControl,
	bool updateStatus, bool deleteTopDir, bool upateFileNameInStatus)
{
	BDirectory dir(dirEntry);

	// loop through everything in folder and delete it, skipping trouble files
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		entry_ref ref;
		entry.GetRef(&ref);

		if (loopControl->CheckUserCanceled())
			return kTrashCanceled;

		status_t status;

		if (entry.IsDirectory())
			status = FSDeleteFolder(&entry, loopControl, updateStatus, true,
				upateFileNameInStatus);
		else {
			status = entry.Remove();
			if (updateStatus) {
				loopControl->UpdateStatus(upateFileNameInStatus ? ref.name
					: "", ref, 1, true);
			}
		}

		if (status == kTrashCanceled)
			return kTrashCanceled;

		if (status != B_OK) {
			loopControl->FileError(B_TRANSLATE_NOCOLLECT(
					kFileDeleteErrorString), ref.name, status, false);
		}
	}

	if (loopControl->CheckUserCanceled())
		return kTrashCanceled;

	entry_ref ref;
	dirEntry->GetRef(&ref);

	if (updateStatus && deleteTopDir)
		loopControl->UpdateStatus(NULL, ref, 1);

	if (deleteTopDir)
		return dirEntry->Remove();

	return B_OK;
}


void
FSMakeOriginalName(BString &string, const BDirectory* destDir,
	const char* suffix)
{
	if (!destDir->Contains(string.String()))
		return;

	FSMakeOriginalName(string.LockBuffer(B_FILE_NAME_LENGTH),
		const_cast<BDirectory*>(destDir), suffix ? suffix : NULL);
	string.UnlockBuffer();
}


void
FSMakeOriginalName(char* name, BDirectory* destDir, const char* suffix)
{
	char		root[B_FILE_NAME_LENGTH];
	char		copybase[B_FILE_NAME_LENGTH];
	char		tempName[B_FILE_NAME_LENGTH + 11];
	int32		fnum;

	// is this name already original?
	if (!destDir->Contains(name))
		return;

	BString copySuffix(B_TRANSLATE_COMMENT("copy", "filename copy"));
	size_t suffixLength = copySuffix.Length();
	if (suffix == NULL)
		suffix = copySuffix;

	// Determine if we're copying a 'copy'. This algorithm isn't perfect.
	// If you're copying a file whose REAL name ends with 'copy' then
	// this method will return "<filename> 1", not "<filename> copy"

	// However, it will correctly handle file that contain 'copy'
	// elsewhere in their name.

	bool copycopy = false;		// are we copying a copy?
	int32 len = (int32)strlen(name);
	char* p = name + len - 1;	// get pointer to end of name

	// eat up optional numbers (if we're copying "<filename> copy 34")
	while ((p > name) && isdigit(*p))
		p--;

	// eat up optional spaces
	while ((p > name) && isspace(*p))
		p--;

	// now look for the phrase " copy"
	if (p > name) {
		// p points to the last char of the word. For example, 'y' in 'copy'

		if ((p - suffixLength > name)
			&& (strncmp(p - suffixLength, suffix, suffixLength + 1) == 0)) {
			// we found 'copy' in the right place.
			// so truncate after 'copy'
			*(p + 1) = '\0';
			copycopy = true;

			// save the 'root' name of the file, for possible later use.
			// that is copy everything but trailing " copy". Need to
			// NULL terminate after copy
			strncpy(root, name, (uint32)((p - name) - suffixLength));
			root[(p - name) - suffixLength] = '\0';
		}
	}

	if (!copycopy) {
		// The name can't be longer than B_FILE_NAME_LENGTH.
		// The algorithm adds a localized " copy XX" to the name.
		// That's the number of characters of "copy" + 4 (spaces + "XX").
		// B_FILE_NAME_LENGTH already accounts for NULL termination so we
		// don't need to save an extra char at the end.
		if (strlen(name) > B_FILE_NAME_LENGTH - (suffixLength + 4)) {
			// name is too long - truncate it!
			name[B_FILE_NAME_LENGTH - (suffixLength + 4)] = '\0';
		}

		strlcpy(root, name, sizeof(root));
			// save root name
		strlcat(name, suffix, B_FILE_NAME_LENGTH);
	}

	strlcpy(copybase, name, sizeof(copybase));

	// if name already exists then add a number
	fnum = 1;
	strlcpy(tempName, name, sizeof(tempName));
	while (destDir->Contains(tempName)) {
		snprintf(tempName, sizeof(tempName), "%s %" B_PRId32, copybase,
			++fnum);

		if (strlen(tempName) > (B_FILE_NAME_LENGTH - 1)) {
			// The name has grown too long. Maybe we just went from
			// "<filename> copy 9" to "<filename> copy 10" and that extra
			// character was too much. The solution is to further
			// truncate the 'root' name and continue.
			// ??? should we reset fnum or not ???
			root[strlen(root) - 1] = '\0';
			snprintf(tempName, sizeof(tempName), "%s%s %" B_PRId32, root,
				suffix, fnum);
		}
	}

	strlcpy(name, tempName, B_FILE_NAME_LENGTH);
}


status_t
FSRecursiveCalcSize(BInfoWindow* window, CopyLoopControl* loopControl,
	BDirectory* dir, off_t* _runningSize, int32* _fileCount, int32* _dirCount)
{
	dir->Rewind();
	BEntry entry;
	while (dir->GetNextEntry(&entry) == B_OK) {
		// be sure window hasn't closed
		if (window && window->StopCalc())
			return B_OK;

		if (loopControl->CheckUserCanceled())
			return kUserCanceled;

		StatStruct statbuf;
		status_t status = entry.GetStat(&statbuf);
		if (status != B_OK)
			return status;

		(*_runningSize) += statbuf.st_blocks * 512;

		if (S_ISDIR(statbuf.st_mode)) {
			BDirectory subdir(&entry);
			(*_dirCount)++;
			status = FSRecursiveCalcSize(window, loopControl, &subdir,
				_runningSize, _fileCount, _dirCount);
			if (status != B_OK)
				return status;
		} else
			(*_fileCount)++;
	}
	return B_OK;
}


status_t
CalcItemsAndSize(CopyLoopControl* loopControl,
	BObjectList<entry_ref, true>* refList, ssize_t blockSize, int32* totalCount,
	off_t* totalSize)
{
	int32 fileCount = 0;
	int32 dirCount = 0;

	// check block size for sanity
	if (blockSize < 0) {
		// This would point at an error to retrieve the block size from
		// the target volume. The code below cannot be used, it is only
		// meant to get the block size when item operations happen on
		// the source volume.
		blockSize = 2048;
	} else if (blockSize < 1024) {
		blockSize = 1024;
		if (entry_ref* ref = refList->ItemAt(0)) {
			// TODO: This assumes all entries in the list share the same
			// volume...
			BVolume volume(ref->device);
			if (volume.InitCheck() == B_OK)
				blockSize = volume.BlockSize();
		}
	}
	// File systems like ReiserFS may advertize a large block size, but
	// stuff is still packed into blocks, so clamp maximum block size.
	if (blockSize > 8192)
		blockSize = 8192;

	int32 num_items = refList->CountItems();
	for (int32 i = 0; i < num_items; i++) {
		entry_ref* ref = refList->ItemAt(i);
		BEntry entry(ref);
		StatStruct statbuf;
		entry.GetStat(&statbuf);

		if (loopControl->CheckUserCanceled())
			return kUserCanceled;

		if (S_ISDIR(statbuf.st_mode)) {
			BDirectory dir(&entry);
			dirCount++;
			(*totalSize) += blockSize;
			status_t result = FSRecursiveCalcSize(NULL, loopControl, &dir,
				totalSize, &fileCount, &dirCount);
			if (result != B_OK)
				return result;
		} else {
			fileCount++;
			(*totalSize) += statbuf.st_size + blockSize;
		}
	}

	*totalCount += (fileCount + dirCount);
	return B_OK;
}


status_t
FSGetTrashDir(BDirectory* trashDir, dev_t dev)
{
	if (trashDir == NULL)
		return B_BAD_VALUE;

	BVolume volume(dev);
	status_t result = volume.InitCheck();
	if (result != B_OK)
		return result;

	BPath path;
	result = find_directory(B_TRASH_DIRECTORY, &path, false, &volume);
	if (result != B_OK)
		return result;

	result = trashDir->SetTo(path.Path());
	if (result != B_OK) {
		// Trash directory does not exist yet, create it.
		result = create_directory(path.Path(), 0755);
		if (result != B_OK)
			return result;

		result = trashDir->SetTo(path.Path());
		if (result != B_OK)
			return result;

		// make Trash directory invisible
		StatStruct sbuf;
		trashDir->GetStat(&sbuf);

		PoseInfo poseInfo;
		poseInfo.fInvisible = true;
		poseInfo.fInitedDirectory = sbuf.st_ino;
		trashDir->WriteAttr(kAttrPoseInfo, B_RAW_TYPE, 0, &poseInfo,
			sizeof(PoseInfo));
	}

	// set Trash icons (if they haven't already been set)
	attr_info attrInfo;
	size_t size;
	const void* data;
	if (trashDir->GetAttrInfo(kAttrLargeIcon, &attrInfo) == B_ENTRY_NOT_FOUND) {
		data = GetTrackerResources()->LoadResource('ICON', R_TrashIcon, &size);
		if (data != NULL)
			trashDir->WriteAttr(kAttrLargeIcon, 'ICON', 0, data, size);
	}

	if (trashDir->GetAttrInfo(kAttrMiniIcon, &attrInfo) == B_ENTRY_NOT_FOUND) {
		data = GetTrackerResources()->LoadResource('MICN', R_TrashIcon, &size);
		if (data != NULL)
			trashDir->WriteAttr(kAttrMiniIcon, 'MICN', 0, data, size);
	}

	if (trashDir->GetAttrInfo(kAttrIcon, &attrInfo) == B_ENTRY_NOT_FOUND) {
		data = GetTrackerResources()->LoadResource(B_VECTOR_ICON_TYPE,
			R_TrashIcon, &size);
		if (data != NULL)
			trashDir->WriteAttr(kAttrIcon, B_VECTOR_ICON_TYPE, 0, data, size);
	}

	return B_OK;
}


status_t
FSGetDeskDir(BDirectory* deskDir)
{
	if (deskDir == NULL)
		return B_BAD_VALUE;

	BPath path;
	status_t result = find_directory(B_DESKTOP_DIRECTORY, &path, true);
	if (result != B_OK)
		return result;

	result = deskDir->SetTo(path.Path());
	if (result != B_OK)
		return result;

	// set Desktop icons (if they haven't already been set)
	attr_info attrInfo;
	size_t size;
	const void* data;
	if (deskDir->GetAttrInfo(kAttrLargeIcon, &attrInfo) == B_ENTRY_NOT_FOUND) {
		data = GetTrackerResources()->LoadResource('ICON', R_DeskIcon, &size);
		if (data != NULL)
			deskDir->WriteAttr(kAttrLargeIcon, 'ICON', 0, data, size);
	}

	if (deskDir->GetAttrInfo(kAttrMiniIcon, &attrInfo) == B_ENTRY_NOT_FOUND) {
		data = GetTrackerResources()->LoadResource('MICN', R_DeskIcon, &size);
		if (data != NULL)
			deskDir->WriteAttr(kAttrMiniIcon, 'MICN', 0, data, size);
	}

	if (deskDir->GetAttrInfo(kAttrIcon, &attrInfo) == B_ENTRY_NOT_FOUND) {
		data = GetTrackerResources()->LoadResource(B_VECTOR_ICON_TYPE,
			R_DeskIcon, &size);
		if (data != NULL)
			deskDir->WriteAttr(kAttrIcon, B_VECTOR_ICON_TYPE, 0, data, size);
	}

	return B_OK;
}


status_t
FSGetBootDeskDir(BDirectory* deskDir)
{
	BVolume bootVolume;
	BVolumeRoster().GetBootVolume(&bootVolume);
	BPath path;

	status_t result = find_directory(B_DESKTOP_DIRECTORY, &path, true,
		&bootVolume);
	if (result != B_OK)
		return result;

	return deskDir->SetTo(path.Path());
}


static bool
FSIsDirFlavor(const BEntry* entry, directory_which directoryType)
{
	StatStruct dir_stat;
	StatStruct entry_stat;
	BVolume volume;
	BPath path;

	if (entry->GetStat(&entry_stat) != B_OK)
		return false;

	if (volume.SetTo(entry_stat.st_dev) != B_OK)
		return false;

	if (find_directory(directoryType, &path, false, &volume) != B_OK)
		return false;

	stat(path.Path(), &dir_stat);

	return dir_stat.st_ino == entry_stat.st_ino
		&& dir_stat.st_dev == entry_stat.st_dev;
}


bool
FSIsPrintersDir(const BEntry* entry)
{
	return FSIsDirFlavor(entry, B_USER_PRINTERS_DIRECTORY);
}


bool
FSIsTrashDir(const BEntry* entry)
{
	return FSIsDirFlavor(entry, B_TRASH_DIRECTORY);
}


bool
FSIsDeskDir(const BEntry* entry)
{
	BPath path;
	status_t result = find_directory(B_DESKTOP_DIRECTORY, &path, true);
	if (result != B_OK)
		return false;

	BEntry entryToCompare(path.Path());
	return entryToCompare == *entry;
}


bool
FSInDeskDir(const entry_ref* ref)
{
	BEntry entry(ref);
	if (entry.InitCheck() != B_OK)
		return false;

	BPath path;
	if (find_directory(B_DESKTOP_DIRECTORY, &path, true) != B_OK)
		return false;

	BDirectory desktop(path.Path());
	return desktop.Contains(&entry);
}


bool
FSIsHomeDir(const BEntry* entry)
{
	return FSIsDirFlavor(entry, B_USER_DIRECTORY);
}


bool
FSIsQueriesDir(const entry_ref* ref)
{
	const BEntry entry(ref);
	return DirectoryMatches(&entry, "queries", B_USER_DIRECTORY);
}


bool
FSIsRootDir(const BEntry* entry)
{
	BPath path;
	if (entry->InitCheck() != B_OK || entry->GetPath(&path) != B_OK)
		return false;

	return strcmp(path.Path(), "/") == 0;
}


bool
FSInRootDir(const entry_ref* ref)
{
	BEntry entry(ref);
	if (entry.InitCheck() != B_OK)
		return false;

	BDirectory root("/");
	return root.Contains(&entry);
}


bool
DirectoryMatchesOrContains(const BEntry* entry, directory_which which)
{
	BPath path;
	if (find_directory(which, &path, false, NULL) != B_OK)
		return false;

	BEntry dirEntry(path.Path());
	if (dirEntry.InitCheck() != B_OK)
		return false;

	if (dirEntry == *entry)
		// root level match
		return true;

	BDirectory dir(&dirEntry);
	return dir.Contains(entry);
}


bool
DirectoryMatchesOrContains(const BEntry* entry, const char* additionalPath,
	directory_which which)
{
	BPath path;
	if (find_directory(which, &path, false, NULL) != B_OK)
		return false;

	path.Append(additionalPath);
	BEntry dirEntry(path.Path());
	if (dirEntry.InitCheck() != B_OK)
		return false;

	if (dirEntry == *entry)
		// root level match
		return true;

	BDirectory dir(&dirEntry);
	return dir.Contains(entry);
}


bool
DirectoryMatches(const BEntry* entry, directory_which which)
{
	BPath path;
	if (find_directory(which, &path, false, NULL) != B_OK)
		return false;

	BEntry dirEntry(path.Path());
	if (dirEntry.InitCheck() != B_OK)
		return false;

	return dirEntry == *entry;
}


bool
DirectoryMatches(const BEntry* entry, const char* additionalPath,
	directory_which which)
{
	BPath path;
	if (find_directory(which, &path, false, NULL) != B_OK)
		return false;

	path.Append(additionalPath);
	BEntry dirEntry(path.Path());
	if (dirEntry.InitCheck() != B_OK)
		return false;

	return dirEntry == *entry;
}


extern status_t
FSFindTrackerSettingsDir(BPath* path, bool autoCreate)
{
	status_t result = find_directory(B_USER_SETTINGS_DIRECTORY, path,
		autoCreate);
	if (result != B_OK)
		return result;

	path->Append("Tracker");

	return mkdir(path->Path(), 0777) ? B_OK : errno;
}


bool
FSInTrashDir(const entry_ref* ref)
{
	BEntry entry(ref);
	if (entry.InitCheck() != B_OK)
		return false;

	BDirectory trashDir;
	if (FSGetTrashDir(&trashDir, ref->device) != B_OK)
		return false;

	return trashDir.Contains(&entry);
}


void
FSEmptyTrash()
{
	if (find_thread("_tracker_empty_trash_") != B_OK) {
		resume_thread(spawn_thread(empty_trash, "_tracker_empty_trash_",
			B_NORMAL_PRIORITY, NULL));
	}
}


status_t
empty_trash(void*)
{
	// empty trash on all mounted volumes
	status_t status = B_OK;

	TrackerCopyLoopControl loopControl(kTrashState);

	// calculate the sum total of all items on all volumes in trash
	BObjectList<entry_ref, true> srcList;
	int32 totalCount = 0;
	off_t totalSize = 0;

	BVolumeRoster volumeRoster;
	BVolume volume;
	while (volumeRoster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsReadOnly() || !volume.IsPersistent())
			continue;

		BDirectory trashDirectory;
		if (FSGetTrashDir(&trashDirectory, volume.Device()) != B_OK)
			continue;

		BEntry entry;
		trashDirectory.GetEntry(&entry);

		entry_ref ref;
		entry.GetRef(&ref);
		srcList.AddItem(&ref);

		status = CalcItemsAndSize(&loopControl, &srcList, volume.BlockSize(),
			&totalCount, &totalSize);
		if (status != B_OK)
			break;

		srcList.RemoveItemAt(0);
			// so that the list doesn't try to free the entry we added above
		srcList.MakeEmpty();

		// don't count trash directory itself
		totalCount--;
	}

	if (status == B_OK) {
		loopControl.Init(totalCount, totalCount);

		volumeRoster.Rewind();
		while (volumeRoster.GetNextVolume(&volume) == B_OK) {
			if (volume.IsReadOnly() || !volume.IsPersistent())
				continue;

			BDirectory trashDirectory;
			if (FSGetTrashDir(&trashDirectory, volume.Device()) != B_OK)
				continue;

			BEntry entry;
			trashDirectory.GetEntry(&entry);
			status = FSDeleteFolder(&entry, &loopControl, true, false);
		}
	}

	if (status != B_OK && status != kTrashCanceled && status != kUserCanceled) {
		BAlert* alert = new BAlert("", B_TRANSLATE("Error emptying Trash"),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
	}

	return B_OK;
}


status_t
_DeleteTask(BObjectList<entry_ref, true>* list, bool confirm)
{
	if (confirm) {
		BAlert* alert = new BAlert("",
			B_TRANSLATE_NOCOLLECT(kDeleteConfirmationStr),
			B_TRANSLATE("Cancel"), B_TRANSLATE("Move to Trash"),
			B_TRANSLATE("Delete"),
			B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);

		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->SetShortcut(0, B_ESCAPE);
		alert->SetShortcut(1, 'm');
		alert->SetShortcut(2, 'd');

		switch (alert->Go()) {
			case 0:
				delete list;
				return B_CANCELED;

			case 1:
			default:
				FSMoveToTrash(list, NULL, false);
				return B_OK;

			case 2:
				break;
		}
	}

	TrackerCopyLoopControl loopControl(kDeleteState);

	// calculate the sum total of all items on all volumes in trash
	int32 totalItems = 0;
	int64 totalSize = 0;

	status_t status = CalcItemsAndSize(&loopControl, list, 0, &totalItems,
		&totalSize);
	if (status == B_OK) {
		loopControl.Init(totalItems, totalItems);

		int32 count = list->CountItems();
		for (int32 index = 0; index < count; index++) {
			entry_ref ref(*list->ItemAt(index));
			BEntry entry(&ref);
			loopControl.UpdateStatus(ref.name, ref, 1, true);
			if (entry.IsDirectory())
				status = FSDeleteFolder(&entry, &loopControl, true, true, true);
			else
				status = entry.Remove();
		}

		if (status != kTrashCanceled && status != kUserCanceled
			&& status != B_OK) {
			BAlert* alert = new BAlert("", B_TRANSLATE("Error deleting items"),
				B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}

	delete list;

	return B_OK;
}

status_t
FSRecursiveCreateFolder(BPath path)
{
	BEntry entry(path.Path());
	if (entry.InitCheck() != B_OK) {
		BPath parentPath;
		status_t err = path.GetParent(&parentPath);
		if (err != B_OK)
			return err;

		err = FSRecursiveCreateFolder(parentPath);
		if (err != B_OK)
			return err;
	}

	entry.SetTo(path.Path());
	if (entry.Exists())
		return B_FILE_EXISTS;

	BDirectory parent;
	entry.GetParent(&parent);
	parent.CreateDirectory(entry.Name(), NULL);

	return B_OK;
}

status_t
_RestoreTask(BObjectList<entry_ref, true>* list)
{
	TrackerCopyLoopControl loopControl(kRestoreFromTrashState);

	// calculate the sum total of all items that will be restored
	int32 totalItems = 0;
	int64 totalSize = 0;

	status_t err = CalcItemsAndSize(&loopControl, list, 0, &totalItems,
		&totalSize);
	if (err == B_OK) {
		loopControl.Init(totalItems, totalItems);

		int32 count = list->CountItems();
		for (int32 index = 0; index < count; index++) {
			entry_ref ref(*list->ItemAt(index));
			BEntry entry(&ref);
			BPath originalPath;

			loopControl.UpdateStatus(ref.name, ref, 1, true);

			if (FSGetOriginalPath(&entry, &originalPath) != B_OK)
				continue;

			BEntry originalEntry(originalPath.Path());
			BPath parentPath;
			err = originalPath.GetParent(&parentPath);
			if (err != B_OK)
				continue;
			BEntry parentEntry(parentPath.Path());

			if (parentEntry.InitCheck() != B_OK || !parentEntry.Exists()) {
				if (FSRecursiveCreateFolder(parentPath) == B_OK) {
					originalEntry.SetTo(originalPath.Path());
					if (entry.InitCheck() != B_OK)
						continue;
				}
			}

			if (!originalEntry.Exists()) {
				BDirectory dir(parentPath.Path());
				if (dir.InitCheck() == B_OK) {
					const char* leafName = originalEntry.Name();
					if (entry.MoveTo(&dir, leafName) == B_OK) {
						BNode node(&entry);
						if (node.InitCheck() == B_OK)
							node.RemoveAttr(kAttrOriginalPath);
					}
				}
			}

			err = loopControl.CheckUserCanceled();
			if (err != B_OK)
				break;
		}
	}

	delete list;

	return err;
}

void
FSCreateTrashDirs()
{
	BVolume volume;
	BVolumeRoster roster;

	roster.Rewind();
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsReadOnly() || !volume.IsPersistent())
			continue;

		BDirectory trashDir;
		FSGetTrashDir(&trashDir, volume.Device());
	}
}


status_t
FSCreateNewFolder(entry_ref* ref)
{
	node_ref node;
	node.device = ref->device;
	node.node = ref->directory;

	BDirectory dir(&node);
	status_t result = dir.InitCheck();
	if (result != B_OK)
		return result;

	// ToDo: is that really necessary here?
	BString name(ref->name);
	FSMakeOriginalName(name, &dir, " -");
	ref->set_name(name.String()); // update ref in case the folder got renamed

	BDirectory newDir;
	result = dir.CreateDirectory(name.String(), &newDir);
	if (result != B_OK)
		return result;

	BNodeInfo nodeInfo(&newDir);
	nodeInfo.SetType(B_DIR_MIMETYPE);

	return result;
}


status_t
FSCreateNewFolderIn(const node_ref* dirNode, entry_ref* newRef,
	node_ref* newNode)
{
	BDirectory dir(dirNode);
	status_t result = dir.InitCheck();
	if (result == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		strlcpy(name, B_TRANSLATE("New folder"), sizeof(name));

		int fnum = 1;
		while (dir.Contains(name)) {
			// if base name already exists then add a number
			// TODO: move this logic to FSMakeOriginalName
			if (++fnum > 9)
				snprintf(name, sizeof(name), B_TRANSLATE("New folder%d"), fnum);
			else
				snprintf(name, sizeof(name), B_TRANSLATE("New folder %d"), fnum);
		}

		BDirectory newDir;
		result = dir.CreateDirectory(name, &newDir);
		if (result == B_OK) {
			BEntry entry;
			newDir.GetEntry(&entry);
			entry.GetRef(newRef);
			entry.GetNodeRef(newNode);

			BNodeInfo nodeInfo(&newDir);
			nodeInfo.SetType(B_DIR_MIMETYPE);

			// add undo item
			NewFolderUndo undo(*newRef);
			return B_OK;
		}
	}

	BAlert* alert = new BAlert("",
		B_TRANSLATE("Sorry, could not create a new folder."),
		B_TRANSLATE("Cancel"), 0, 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();

	return result;
}


ReadAttrResult
ReadAttr(const BNode* node, const char* hostAttrName,
	const char* foreignAttrName, type_code type, off_t offset, void* buffer,
	size_t length, void (*swapFunc)(void*), bool isForeign)
{
	if (!isForeign && node->ReadAttr(hostAttrName, type, offset, buffer,
			length) == (ssize_t)length) {
		return kReadAttrNativeOK;
	}

	// PRINT(("trying %s\n", foreignAttrName));
	// try the other endianness
	if (node->ReadAttr(foreignAttrName, type, offset, buffer, length)
			!= (ssize_t)length) {
		return kReadAttrFailed;
	}

	// PRINT(("got %s\n", foreignAttrName));
	if (!swapFunc)
		return kReadAttrForeignOK;

	(swapFunc)(buffer);
		// run the endian swapper

	return kReadAttrForeignOK;
}


ReadAttrResult
GetAttrInfo(const BNode* node, const char* hostAttrName,
	const char* foreignAttrName, type_code* type, size_t* size)
{
	attr_info info;

	if (node->GetAttrInfo(hostAttrName, &info) == B_OK) {
		if (type)
			*type = info.type;
		if (size)
			*size = (size_t)info.size;

		return kReadAttrNativeOK;
	}

	if (node->GetAttrInfo(foreignAttrName, &info) == B_OK) {
		if (type)
			*type = info.type;
		if (size)
			*size = (size_t)info.size;

		return kReadAttrForeignOK;
	}
	return kReadAttrFailed;
}


status_t
FSGetParentVirtualDirectoryAware(const BEntry& entry, entry_ref& _ref)
{
	node_ref nodeRef;
	if (entry.GetNodeRef(&nodeRef) == B_OK) {
		if (VirtualDirectoryManager* manager
				= VirtualDirectoryManager::Instance()) {
			AutoLocker<VirtualDirectoryManager> managerLocker(manager);
			if (manager->GetParentDirectoryDefinitionFile(nodeRef, _ref,
					nodeRef)) {
				return B_OK;
			}
		}
	}

	status_t error;
	BDirectory parent;
	BEntry parentEntry;
	if ((error = entry.GetParent(&parent)) != B_OK
		|| (error = parent.GetEntry(&parentEntry)) != B_OK
		|| (error = parentEntry.GetRef(&_ref)) != B_OK) {
		return error;
	}

	return B_OK;
}


status_t
FSGetParentVirtualDirectoryAware(const BEntry& entry, BEntry& _entry)
{
	node_ref nodeRef;
	if (entry.GetNodeRef(&nodeRef) == B_OK) {
		if (VirtualDirectoryManager* manager
				= VirtualDirectoryManager::Instance()) {
			AutoLocker<VirtualDirectoryManager> managerLocker(manager);
			entry_ref parentRef;
			if (manager->GetParentDirectoryDefinitionFile(nodeRef, parentRef,
					nodeRef)) {
				return _entry.SetTo(&parentRef);
			}
		}
	}

	return entry.GetParent(&_entry);
}


status_t
FSGetParentVirtualDirectoryAware(const BEntry& entry, BNode& _node)
{
	entry_ref ref;
	status_t error = FSGetParentVirtualDirectoryAware(entry, ref);
	if (error == B_OK)
		error = _node.SetTo(&ref);

	return error;
}


// launching code

static status_t
TrackerOpenWith(const BMessage* refs)
{
	BMessage clone(*refs);

	ASSERT(dynamic_cast<TTracker*>(be_app) != NULL);
	ASSERT(clone.what != 0);

	clone.AddInt32("launchUsingSelector", 0);
	// runs the Open With window
	be_app->PostMessage(&clone);

	return B_OK;
}


static void
AsynchLaunchBinder(void (*func)(const entry_ref*, const BMessage*, bool on),
	const entry_ref* appRef, const BMessage* refs, bool openWithOK)
{
	BMessage* task = new BMessage;
	task->AddPointer("function", (void*)func);
	task->AddMessage("refs", refs);
	task->AddBool("openWithOK", openWithOK);
	if (appRef != NULL)
		task->AddRef("appRef", appRef);

	extern BLooper* gLaunchLooper;
	gLaunchLooper->PostMessage(task);
}


static bool
SniffIfGeneric(const entry_ref* ref)
{
	BNode node(ref);
	char type[B_MIME_TYPE_LENGTH];
	BNodeInfo info(&node);
	if (info.GetType(type) == B_OK
		&& strcasecmp(type, B_FILE_MIME_TYPE) != 0) {
		// already has a type and it's not octet stream
		return false;
	}

	BPath path(ref);
	if (path.Path()) {
		// force a mimeset
		node.RemoveAttr(kAttrMIMEType);
		update_mime_info(path.Path(), 0, 1, 1);
	}

	return true;
}


static void
SniffIfGeneric(const BMessage* refs)
{
	entry_ref ref;
	for (int32 index = 0; ; index++) {
		if (refs->FindRef("refs", index, &ref) != B_OK)
			break;
		SniffIfGeneric(&ref);
	}
}


static void
_TrackerLaunchAppWithDocuments(const entry_ref* appRef, const BMessage* refs,
	bool openWithOK)
{
	team_id team;

	status_t error = B_ERROR;
	BString alertString;

	for (int32 mimesetIt = 0; ; mimesetIt++) {
		error = be_roster->Launch(appRef, refs, &team);
		if (error == B_ALREADY_RUNNING)
			// app already running, not really an error
			error = B_OK;

		if (error == B_OK)
			break;

		if (mimesetIt > 0)
			break;

		// failed to open, try mimesetting the refs and launching again
		SniffIfGeneric(refs);
	}

	if (error == B_OK) {
		// close possible parent window, if specified
		const node_ref* nodeToClose = 0;
		ssize_t numBytes;
		if (refs != NULL && refs->FindData("nodeRefsToClose", B_RAW_TYPE,
				(const void**)&nodeToClose, &numBytes) == B_OK
			&& nodeToClose != NULL) {
			TTracker* tracker = dynamic_cast<TTracker*>(be_app);
			if (tracker != NULL)
				tracker->CloseParent(*nodeToClose);
		}
	} else {
		alertString.SetTo(B_TRANSLATE("Could not open \"%name\" (%error). "));
		alertString.ReplaceFirst("%name", appRef->name);
		alertString.ReplaceFirst("%error", strerror(error));
		if (refs != NULL && openWithOK && error != B_SHUTTING_DOWN) {
			alertString << B_TRANSLATE_NOCOLLECT(kFindAlternativeStr);
			BAlert* alert = new BAlert("", alertString.String(),
				B_TRANSLATE("Cancel"), B_TRANSLATE("Find"), 0,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() == 1)
				error = TrackerOpenWith(refs);
		} else {
			BAlert* alert = new BAlert("", alertString.String(),
				B_TRANSLATE("Cancel"), 0, 0, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}
}


extern "C" char** environ;


static status_t
LoaderErrorDetails(const entry_ref* app, BString &details)
{
	BPath path;
	BEntry appEntry(app, true);

	status_t result = appEntry.GetPath(&path);
	if (result != B_OK)
		return result;

	char* argv[2] = { const_cast<char*>(path.Path()), 0};

	port_id errorPort = create_port(1, "Tracker loader error");

	// count environment variables
	int32 envCount = 0;
	while (environ[envCount] != NULL)
		envCount++;

	char** flatArgs = NULL;
	size_t flatArgsSize;
	result = __flatten_process_args((const char**)argv, 1,
		environ, &envCount, argv[0], &flatArgs, &flatArgsSize);
	if (result != B_OK)
		return result;

	result = _kern_load_image(flatArgs, flatArgsSize, 1, envCount,
		B_NORMAL_PRIORITY, B_WAIT_TILL_LOADED, errorPort, 0);
	if (result == B_OK) {
		// we weren't supposed to be able to start the application...
		return B_ERROR;
	}

	// read error message from port and construct details string

	ssize_t bufferSize;

	do {
		bufferSize = port_buffer_size_etc(errorPort, B_RELATIVE_TIMEOUT, 0);
	} while (bufferSize == B_INTERRUPTED);

	if (bufferSize <= B_OK) {
		delete_port(errorPort);
		return bufferSize;
	}

	uint8* buffer = (uint8*)malloc(bufferSize);
	if (buffer == NULL) {
		delete_port(errorPort);
		return B_NO_MEMORY;
	}

	bufferSize = read_port_etc(errorPort, NULL, buffer, bufferSize,
		B_RELATIVE_TIMEOUT, 0);
	delete_port(errorPort);

	if (bufferSize < B_OK) {
		free(buffer);
		return bufferSize;
	}

	BMessage message;
	result = message.Unflatten((const char*)buffer);
	free(buffer);

	if (result != B_OK)
		return result;

	int32 errorCode = B_ERROR;
	result = message.FindInt32("error", &errorCode);
	if (result != B_OK)
		return result;

	const char* detailName = NULL;
	switch (errorCode) {
		case B_MISSING_LIBRARY:
			detailName = "missing library";
			break;

		case B_MISSING_SYMBOL:
			detailName = "missing symbol";
			break;
	}

	if (detailName == NULL)
		return B_ERROR;

	const char* detail;
	for (int32 i = 0; message.FindString(detailName, i, &detail) == B_OK;
			i++) {
		if (i > 0)
			details += ", ";
		details += detail;
	}

	return B_OK;
}


static void
_TrackerLaunchDocuments(const entry_ref*, const BMessage* refs,
	bool openWithOK)
{
	if (refs == NULL)
		return;

	BMessage copyOfRefs(*refs);

	entry_ref documentRef;
	if (copyOfRefs.FindRef("refs", &documentRef) != B_OK) {
		// nothing to launch, we are done
		return;
	}

	status_t error = B_ERROR;
	entry_ref app;
	BMessage* refsToPass = NULL;
	BString alertString;
	const char* alternative = 0;

	for (int32 mimesetIt = 0; ; mimesetIt++) {
		alertString = "";
		error = be_roster->FindApp(&documentRef, &app);

		if (error != B_OK && mimesetIt == 0) {
			SniffIfGeneric(&copyOfRefs);
			continue;
		}

		if (error != B_OK) {
			alertString.SetTo(B_TRANSLATE("Could not find an application to "
				"open \"%name\" (%error). "));
			alertString.ReplaceFirst("%name", documentRef.name);
			alertString.ReplaceFirst("%error", strerror(error));
			if (openWithOK)
				alternative = B_TRANSLATE_NOCOLLECT(kFindApplicationStr);

			break;
		} else {
			BEntry appEntry(&app, true);
			for (int32 index = 0;;) {
				// remove the app itself from the refs received so we don't
				// try to open ourselves
				entry_ref ref;
				if (copyOfRefs.FindRef("refs", index, &ref) != B_OK)
					break;

				// deal with symlinks properly
				BEntry documentEntry(&ref, true);
				if (appEntry == documentEntry) {
					PRINT(("stripping %s, app %s \n", ref.name, app.name));
					copyOfRefs.RemoveData("refs", index);
				} else {
					PRINT(("leaving %s, app %s  \n", ref.name, app.name));
					index++;
				}
			}

			refsToPass = CountRefs(&copyOfRefs) > 0 ? &copyOfRefs: 0;
			team_id team;
			error = be_roster->Launch(&app, refsToPass, &team);
			if (error == B_ALREADY_RUNNING)
				// app already running, not really an error
				error = B_OK;
			if (error == B_OK || mimesetIt != 0)
				break;
			if (error == B_LAUNCH_FAILED_EXECUTABLE) {
				BVolume volume(documentRef.device);
				if (volume.IsReadOnly()) {
					BMimeType type;
					error = BMimeType::GuessMimeType(&documentRef, &type);
					if (error != B_OK)
						break;
					error = be_roster->FindApp(type.Type(), &app);
					if (error != B_OK)
						break;
					error = be_roster->Launch(&app, refs, &team);
					if (error == B_ALREADY_RUNNING)
						// app already running, not really an error
						error = B_OK;
					if (error == B_OK || mimesetIt != 0)
						break;
				}
			}

			SniffIfGeneric(&copyOfRefs);
		}
	}

	if (error != B_OK && alertString.Length() == 0) {
		BString loaderErrorString;
		bool openedDocuments = true;

		if (!refsToPass) {
			// we just double clicked the app itself, do not offer to
			// find a handling app
			openWithOK = false;
			openedDocuments = false;
		}
		if (error == B_UNKNOWN_EXECUTABLE && !refsToPass) {
			// We know it's an executable, but something unsupported
			alertString.SetTo(B_TRANSLATE("\"%name\" is an unsupported "
				"executable."));
			alertString.ReplaceFirst("%name", app.name);
		} else if (error == B_LEGACY_EXECUTABLE && !refsToPass) {
			// For the moment, this marks an old R3 binary, we may want to
			// extend it to gcc2 binaries someday post R1
			alertString.SetTo(B_TRANSLATE("\"%name\" is a legacy executable. "
				"Please obtain an updated version or recompile "
				"the application."));
			alertString.ReplaceFirst("%name", app.name);
		} else if (error == B_LAUNCH_FAILED_EXECUTABLE && !refsToPass) {
			alertString.SetTo(B_TRANSLATE("Could not open \"%name\". "
				"The file is mistakenly marked as executable. "));
			alertString.ReplaceFirst("%name", app.name);

			if (!openWithOK) {
				// offer the possibility to change the permissions

				alertString << B_TRANSLATE("\nShould this be fixed?");
				BAlert* alert = new BAlert("", alertString.String(),
					B_TRANSLATE("Cancel"), B_TRANSLATE("Proceed"), 0,
					B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->SetShortcut(0, B_ESCAPE);
				if (alert->Go() == 1) {
					BEntry entry(&documentRef);
					mode_t permissions;

					error = entry.GetPermissions(&permissions);
					if (error == B_OK) {
						error = entry.SetPermissions(permissions
							& ~(S_IXUSR | S_IXGRP | S_IXOTH));
					}
					if (error == B_OK) {
						// we updated the permissions, so let's try again
						_TrackerLaunchDocuments(NULL, refs, false);
						return;
					} else {
						alertString.SetTo(B_TRANSLATE("Could not update "
							"permissions of file \"%name\". %error"));
						alertString.ReplaceFirst("%name", app.name);
						alertString.ReplaceFirst("%error", strerror(error));
					}
				} else
					return;
			}

			alternative = B_TRANSLATE_NOCOLLECT(kFindApplicationStr);
		} else if (error == B_LAUNCH_FAILED_APP_IN_TRASH) {
			alertString.SetTo(B_TRANSLATE("Could not open \"%document\" "
				"because application \"%app\" is in the Trash. "));
			alertString.ReplaceFirst("%document", documentRef.name);
			alertString.ReplaceFirst("%app", app.name);
			alternative = B_TRANSLATE_NOCOLLECT(kFindAlternativeStr);
		} else if (error == B_LAUNCH_FAILED_APP_NOT_FOUND) {
			alertString.SetTo(
				B_TRANSLATE("Could not open \"%name\" (%error). "));
			alertString.ReplaceFirst("%name", documentRef.name);
			alertString.ReplaceFirst("%error", strerror(error));
			alternative = B_TRANSLATE_NOCOLLECT(kFindAlternativeStr);
		} else if (error == B_MISSING_SYMBOL
			&& LoaderErrorDetails(&app, loaderErrorString) == B_OK) {
			if (openedDocuments) {
				alertString.SetTo(B_TRANSLATE("Could not open \"%document\" "
					"with application \"%app\" (Missing symbol: %symbol). "
					"\n"));
				alertString.ReplaceFirst("%document", documentRef.name);
				alertString.ReplaceFirst("%app", app.name);
				alertString.ReplaceFirst("%symbol",
					loaderErrorString.String());
			} else {
				alertString.SetTo(B_TRANSLATE("Could not open \"%document\" "
					"(Missing symbol: %symbol). \n"));
				alertString.ReplaceFirst("%document", documentRef.name);
				alertString.ReplaceFirst("%symbol",
					loaderErrorString.String());
			}
			alternative = B_TRANSLATE_NOCOLLECT(kFindAlternativeStr);
		} else if (error == B_MISSING_LIBRARY
			&& LoaderErrorDetails(&app, loaderErrorString) == B_OK) {
			if (openedDocuments) {
				alertString.SetTo(B_TRANSLATE("Could not open \"%document\" "
					"with application \"%app\" (Missing libraries: %library). "
					"\n"));
				alertString.ReplaceFirst("%document", documentRef.name);
				alertString.ReplaceFirst("%app", app.name);
				alertString.ReplaceFirst("%library",
					loaderErrorString.String());
			} else {
				alertString.SetTo(B_TRANSLATE("Could not open \"%document\" "
					"(Missing libraries: %library). \n"));
				alertString.ReplaceFirst("%document", documentRef.name);
				alertString.ReplaceFirst("%library",
					loaderErrorString.String());
			}
			alternative = B_TRANSLATE_NOCOLLECT(kFindAlternativeStr);
		} else {
			alertString.SetTo(B_TRANSLATE("Could not open \"%document\" with "
				"application \"%app\" (%error). "));
				alertString.ReplaceFirst("%document", documentRef.name);
				alertString.ReplaceFirst("%app", app.name);
				alertString.ReplaceFirst("%error", strerror(error));
			alternative = B_TRANSLATE_NOCOLLECT(kFindAlternativeStr);
		}
	}

	if (error != B_OK) {
		if (openWithOK) {
			ASSERT(alternative);
			alertString << alternative;
			BAlert* alert = new BAlert("", alertString.String(),
				B_TRANSLATE("Cancel"), B_TRANSLATE("Find"), 0,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() == 1)
				error = TrackerOpenWith(refs);
		} else {
			BAlert* alert = new BAlert("", alertString.String(),
				B_TRANSLATE("Cancel"), 0, 0, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}
}

// the following three calls don't return any reasonable error codes,
// should fix that, making them void

status_t
TrackerLaunch(const entry_ref* appRef, const BMessage* refs, bool async,
	bool openWithOK)
{
	if (!async)
		_TrackerLaunchAppWithDocuments(appRef, refs, openWithOK);
	else {
		AsynchLaunchBinder(&_TrackerLaunchAppWithDocuments, appRef, refs,
			openWithOK);
	}

	return B_OK;
}

status_t
TrackerLaunch(const entry_ref* appRef, bool async)
{
	if (!async)
		_TrackerLaunchAppWithDocuments(appRef, NULL, false);
	else
		AsynchLaunchBinder(&_TrackerLaunchAppWithDocuments, appRef, 0, false);

	return B_OK;
}

status_t
TrackerLaunch(const BMessage* refs, bool async, bool openWithOK)
{
	if (!async)
		_TrackerLaunchDocuments(NULL, refs, openWithOK);
	else
		AsynchLaunchBinder(&_TrackerLaunchDocuments, NULL, refs, openWithOK);

	return B_OK;
}


// external launch calls; need to be robust, work if Tracker is not running


#if !B_BEOS_VERSION_DANO
_IMPEXP_TRACKER
#endif
status_t
FSLaunchItem(const entry_ref* application, const BMessage* refsReceived,
	bool async, bool openWithOK)
{
	return TrackerLaunch(application, refsReceived, async, openWithOK);
}


#if !B_BEOS_VERSION_DANO
_IMPEXP_TRACKER
#endif
status_t
FSOpenWith(BMessage* listOfRefs)
{
	status_t result = B_ERROR;
	listOfRefs->what = B_REFS_RECEIVED;

	if (dynamic_cast<TTracker*>(be_app) != NULL)
		result = TrackerOpenWith(listOfRefs);
	else
		ASSERT(!"not yet implemented");

	return result;
}


// legacy calls, need for compatibility


void
FSOpenWithDocuments(const entry_ref* executable, BMessage* documents)
{
	TrackerLaunch(executable, documents, true);
	delete documents;
}


status_t
FSLaunchUsing(const entry_ref* ref, BMessage* listOfRefs)
{
	BMessage temp(B_REFS_RECEIVED);
	if (listOfRefs == NULL) {
		ASSERT(ref != NULL);
		temp.AddRef("refs", ref);
		listOfRefs = &temp;
	}
	FSOpenWith(listOfRefs);

	return B_OK;
}


status_t
FSLaunchItem(const entry_ref* appRef, BMessage* refs, int32, bool async)
{
	if (refs != NULL)
		refs->what = B_REFS_RECEIVED;

	status_t result = TrackerLaunch(appRef, refs, async, true);
	delete refs;

	return result;
}


void
FSLaunchItem(const entry_ref* appRef, BMessage* refs, int32 workspace)
{
	FSLaunchItem(appRef, refs, workspace, true);
}


// Get the original path of an entry in the trash
status_t
FSGetOriginalPath(BEntry* entry, BPath* result)
{
	status_t err;
	entry_ref ref;
	err = entry->GetRef(&ref);
	if (err != B_OK)
		return err;

	// Only call the routine for entries in the trash
	if (!FSInTrashDir(&ref))
		return B_ERROR;

	BNode node(entry);
	BString originalPath;
	if (node.ReadAttrString(kAttrOriginalPath, &originalPath) == B_OK) {
		// We're in luck, the entry has the original path in an attribute
		err = result->SetTo(originalPath.String());
		return err;
	}

	// Iterate the parent directories to find one with
	// the original path attribute
	BEntry parent(*entry);
	err = parent.InitCheck();
	if (err != B_OK)
		return err;

	// walk up the directory structure until we find a node
	// with original path attribute
	do {
		// move to the parent of this node
		err = parent.GetParent(&parent);
		if (err != B_OK)
			return err;

		// return if we are at the root of the trash
		if (FSIsTrashDir(&parent))
			return B_ENTRY_NOT_FOUND;

		// get the parent as a node
		err = node.SetTo(&parent);
		if (err != B_OK)
			return err;
	} while (node.ReadAttrString(kAttrOriginalPath, &originalPath) != B_OK);

	// Found the attribute, figure out there this file
	// used to live, based on the successfully-read attribute
	err = result->SetTo(originalPath.String());
	if (err != B_OK)
		return err;

	BPath path, pathParent;
	err = parent.GetPath(&pathParent);
	if (err != B_OK)
		return err;

	err = entry->GetPath(&path);
	if (err != B_OK)
		return err;

	result->Append(path.Path() + strlen(pathParent.Path()) + 1);
		// compute the new path by appending the offset of
		// the item we are locating, to the original path
		// of the parent

	return B_OK;
}


directory_which
WellKnowEntryList::Match(const node_ref* node)
{
	const WellKnownEntry* result = MatchEntry(node);
	if (result != NULL)
		return result->which;

	return (directory_which)-1;
}


const WellKnowEntryList::WellKnownEntry*
WellKnowEntryList::MatchEntry(const node_ref* node)
{
	if (self == NULL)
		self = new WellKnowEntryList();

	return self->MatchEntryCommon(node);
}


const WellKnowEntryList::WellKnownEntry*
WellKnowEntryList::MatchEntryCommon(const node_ref* node)
{
	uint32 count = entries.size();
	for (uint32 index = 0; index < count; index++) {
		if (*node == entries[index].node)
			return &entries[index];
	}

	return NULL;
}


void
WellKnowEntryList::Quit()
{
	delete self;
	self = NULL;
}


void
WellKnowEntryList::AddOne(directory_which which, const char* name)
{
	BPath path;
	if (find_directory(which, &path, true) != B_OK)
		return;

	BEntry entry(path.Path(), true);
	node_ref node;
	if (entry.GetNodeRef(&node) != B_OK)
		return;

	entries.push_back(WellKnownEntry(&node, which, name));
}


void
WellKnowEntryList::AddOne(directory_which which, directory_which base,
	const char* extra, const char* name)
{
	BPath path;
	if (find_directory(base, &path, true) != B_OK)
		return;

	path.Append(extra);
	BEntry entry(path.Path(), true);
	node_ref node;
	if (entry.GetNodeRef(&node) != B_OK)
		return;

	entries.push_back(WellKnownEntry(&node, which, name));
}


void
WellKnowEntryList::AddOne(directory_which which, const char* path,
	const char* name)
{
	BEntry entry(path, true);
	node_ref node;
	if (entry.GetNodeRef(&node) != B_OK)
		return;

	entries.push_back(WellKnownEntry(&node, which, name));
}


WellKnowEntryList::WellKnowEntryList()
{
	AddOne(B_SYSTEM_DIRECTORY, "system");
	AddOne((directory_which)B_BOOT_DISK, "/boot", "boot");
	AddOne(B_USER_DIRECTORY, "home");

	AddOne(B_BEOS_FONTS_DIRECTORY, "fonts");
	AddOne(B_USER_FONTS_DIRECTORY, "fonts");

	AddOne(B_BEOS_APPS_DIRECTORY, "apps");
	AddOne(B_APPS_DIRECTORY, "apps");
	AddOne((directory_which)B_USER_DESKBAR_APPS_DIRECTORY,
		B_USER_DESKBAR_DIRECTORY, "Applications", "apps");

	AddOne(B_BEOS_PREFERENCES_DIRECTORY, "preferences");
	AddOne(B_PREFERENCES_DIRECTORY, "preferences");
	AddOne((directory_which)B_USER_DESKBAR_PREFERENCES_DIRECTORY,
		B_USER_DESKBAR_DIRECTORY, "Preferences", "preferences");

	AddOne((directory_which)B_USER_MAIL_DIRECTORY, B_USER_DIRECTORY, "mail",
		"mail");

	AddOne((directory_which)B_USER_QUERIES_DIRECTORY, B_USER_DIRECTORY,
		"queries", "queries");

	AddOne(B_SYSTEM_DEVELOP_DIRECTORY, "develop");
	AddOne((directory_which)B_USER_DESKBAR_DEVELOP_DIRECTORY,
		B_USER_DESKBAR_DIRECTORY, "Development", "develop");

	AddOne(B_USER_CONFIG_DIRECTORY, "config");

	AddOne((directory_which)B_USER_PEOPLE_DIRECTORY, B_USER_DIRECTORY,
		"people", "people");

	AddOne((directory_which)B_USER_DOWNLOADS_DIRECTORY, B_USER_DIRECTORY,
		"downloads", "downloads");
}

WellKnowEntryList* WellKnowEntryList::self = NULL;

} // namespace BPrivate
