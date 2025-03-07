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

//	Dedicated to BModel

// ToDo:
// Consider moving iconFrom logic to BPose
// use a more efficient way of storing file type and preferred app strings


#include "Model.h"

#include <stdlib.h>
#include <strings.h>

#include <fs_info.h>
#include <fs_attr.h>

#include <AppDefs.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Locale.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <SymLink.h>
#include <StringList.h>
#include <Query.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "Attributes.h"
#include "Bitmaps.h"
#include "FindPanel.h"
#include "FSUtils.h"
#include "MimeTypes.h"
#include "Thumbnails.h"
#include "Tracker.h"
#include "Utilities.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Model"


#ifdef CHECK_OPEN_MODEL_LEAKS
BObjectList<Model>* writableOpenModelList = NULL;
BObjectList<Model>* readOnlyOpenModelList = NULL;
#endif


//	#pragma mark - Model()


Model::Model()
	:
	fPreferredAppName(NULL),
	fBaseType(kUnknownNode),
	fIconFrom(kUnknownSource),
	fWritable(false),
	fNode(NULL),
	fStatus(B_NO_INIT),
	fHasLocalizedName(false),
	fLocalizedNameIsCached(false)
{
}


Model::Model(const Model& other)
	:
	fEntryRef(other.fEntryRef),
	fMimeType(other.fMimeType),
	fPreferredAppName(NULL),
	fBaseType(other.fBaseType),
	fIconFrom(other.fIconFrom),
	fWritable(false),
	fNode(NULL),
	fLocalizedName(other.fLocalizedName),
	fHasLocalizedName(other.fHasLocalizedName),
	fLocalizedNameIsCached(other.fLocalizedNameIsCached)
{
	fStatBuf.st_dev = other.NodeRef()->device;
	fStatBuf.st_ino = other.NodeRef()->node;

	if (other.IsSymLink() && other.LinkTo())
		fLinkTo = new Model(*other.LinkTo());

	fStatus = OpenNode(other.IsNodeOpenForWriting());
	if (fStatus == B_OK) {
		ASSERT(fNode);
		fNode->GetStat(&fStatBuf);
		ASSERT(fStatBuf.st_dev == other.NodeRef()->device);
		ASSERT(fStatBuf.st_ino == other.NodeRef()->node);
	}
	if (!other.IsNodeOpen())
		CloseNode();
}


Model::Model(const node_ref* dirNode, const node_ref* node, const char* name,
	bool open, bool writable)
	:
	fPreferredAppName(NULL),
	fWritable(false),
	fNode(NULL),
	fHasLocalizedName(false),
	fLocalizedNameIsCached(false)
{
	SetTo(dirNode, node, name, open, writable);
}


Model::Model(const BEntry* entry, bool open, bool writable)
	:
	fPreferredAppName(NULL),
	fWritable(false),
	fNode(NULL),
	fHasLocalizedName(false),
	fLocalizedNameIsCached(false)
{
	SetTo(entry, open, writable);
}


Model::Model(const entry_ref* ref, bool traverse, bool open, bool writable)
	:
	fPreferredAppName(NULL),
	fBaseType(kUnknownNode),
	fIconFrom(kUnknownSource),
	fWritable(false),
	fNode(NULL),
	fHasLocalizedName(false),
	fLocalizedNameIsCached(false)
{
	BEntry entry(ref, traverse);
	fStatus = entry.InitCheck();
	if (fStatus == B_OK)
		SetTo(&entry, open, writable);
}


void
Model::DeletePreferredAppVolumeNameLinkTo()
{
	if (IsSymLink()) {
		Model* tmp = fLinkTo;
			// deal with link to link to self
		fLinkTo = NULL;
		delete tmp;
	} else if (IsVolume())
		free(fVolumeName);
	else
		free(fPreferredAppName);

	fPreferredAppName = NULL;
}


Model::~Model()
{
#ifdef CHECK_OPEN_MODEL_LEAKS
	if (writableOpenModelList != NULL)
		writableOpenModelList->RemoveItem(this);

	if (readOnlyOpenModelList != NULL)
		readOnlyOpenModelList->RemoveItem(this);
#endif

	DeletePreferredAppVolumeNameLinkTo();
	if (IconCache::NeedsDeletionNotification((IconSource)fIconFrom)) {
		// this check allows us to use temporary Model in the IconCache
		// without the danger of a deadlock
		IconCache::sIconCache->Deleting(this);
	}
#if xDEBUG
	if (fNode != NULL)
		PRINT(("destructor closing node for %s\n", Name()));
#endif

	delete fNode;
}


status_t
Model::SetTo(const BEntry* entry, bool open, bool writable)
{
	delete fNode;
	fNode = NULL;
	DeletePreferredAppVolumeNameLinkTo();
	fIconFrom = kUnknownSource;
	fBaseType = kUnknownNode;
	fMimeType = "";

	fStatus = entry->GetRef(&fEntryRef);
	if (fStatus != B_OK)
		return fStatus;

	fStatus = entry->GetStat(&fStatBuf);
	if (fStatus != B_OK)
		return fStatus;

	fStatus = OpenNode(writable);
	if (!open)
		CloseNode();

	return fStatus;
}


status_t
Model::SetTo(const entry_ref* newRef, bool traverse, bool open, bool writable)
{
	delete fNode;
	fNode = NULL;
	DeletePreferredAppVolumeNameLinkTo();
	fIconFrom = kUnknownSource;
	fBaseType = kUnknownNode;
	fMimeType = "";

	BEntry tmpEntry(newRef, traverse);
	fStatus = tmpEntry.InitCheck();
	if (fStatus != B_OK)
		return fStatus;

	if (traverse)
		tmpEntry.GetRef(&fEntryRef);
	else
		fEntryRef = *newRef;

	fStatus = tmpEntry.GetStat(&fStatBuf);
	if (fStatus != B_OK)
		return fStatus;

	fStatus = OpenNode(writable);
	if (!open)
		CloseNode();

	return fStatus;
}


status_t
Model::SetTo(const node_ref* dirNode, const node_ref* nodeRef,
	const char* name, bool open, bool writable)
{
	delete fNode;
	fNode = NULL;
	DeletePreferredAppVolumeNameLinkTo();
	fIconFrom = kUnknownSource;
	fBaseType = kUnknownNode;
	fMimeType = "";

	fStatBuf.st_dev = nodeRef->device;
	fStatBuf.st_ino = nodeRef->node;
	fEntryRef.device = dirNode->device;
	fEntryRef.directory = dirNode->node;
	fEntryRef.name = strdup(name);

	BEntry tmpNode(&fEntryRef);
	fStatus = tmpNode.InitCheck();
	if (fStatus != B_OK)
		return fStatus;

	fStatus = tmpNode.GetStat(&fStatBuf);
	if (fStatus != B_OK)
		return fStatus;

	fStatus = OpenNode(writable);

	if (!open)
		CloseNode();

	return fStatus;
}


status_t
Model::InitCheck() const
{
	return fStatus;
}


int
Model::CompareFolderNamesFirst(const Model* compareModel) const
{
	if (compareModel == NULL)
		return -1;

	const Model* resolvedCompareModel = compareModel->ResolveIfLink();
	const Model* resolvedMe = ResolveIfLink();

	bool meIsDirOrVolume = resolvedMe->IsDirectory() || resolvedMe->IsVolume()
		|| resolvedMe->IsVirtualDirectory();
	bool otherIsDirOrVolume = resolvedCompareModel->IsDirectory()
		|| resolvedCompareModel->IsVolume()
		|| resolvedCompareModel->IsVirtualDirectory();

	if (meIsDirOrVolume) {
		if (!otherIsDirOrVolume)
			return -1;
	} else if (otherIsDirOrVolume)
		return 1;

	return NaturalCompare(Name(), compareModel->Name());
}


const char*
Model::Name() const
{
	static const char* kRootNodeName = B_TRANSLATE_MARK(B_DISKS_DIR_NAME);
	static const char* kTrashNodeName = B_TRANSLATE_MARK(B_TRASH_DIR_NAME);
	static const char* kDesktopNodeName = B_TRANSLATE_MARK(B_DESKTOP_DIR_NAME);

	switch (fBaseType) {
		case kRootNode:
			return B_TRANSLATE_NOCOLLECT(kRootNodeName);

		case kVolumeNode:
			if (fVolumeName != NULL)
				return fVolumeName;
			break;

		case kTrashNode:
			return B_TRANSLATE_NOCOLLECT(kTrashNodeName);

		case kDesktopNode:
			return B_TRANSLATE_NOCOLLECT(kDesktopNodeName);

		default:
			break;
	}

	if (fHasLocalizedName && gLocalizedNamePreferred)
		return fLocalizedName.String();
	else
		return fEntryRef.name;
}


status_t
Model::OpenNode(bool writable)
{
	if (IsNodeOpen() && (writable == IsNodeOpenForWriting()))
		return B_OK;

	OpenNodeCommon(writable);

	return fStatus;
}


status_t
Model::UpdateStatAndOpenNode(bool writable)
{
	if (IsNodeOpen() && (writable == IsNodeOpenForWriting()))
		return B_OK;

	// try reading the stat structure again
	BEntry tmpEntry(&fEntryRef);
	fStatus = tmpEntry.InitCheck();
	if (fStatus != B_OK)
		return fStatus;

	fStatus = tmpEntry.GetStat(&fStatBuf);
	if (fStatus != B_OK)
		return fStatus;

	OpenNodeCommon(writable);

	return fStatus;
}


status_t
Model::OpenNodeCommon(bool writable)
{
#if xDEBUG
	PRINT(("opening node for %s\n", Name()));
#endif

#ifdef CHECK_OPEN_MODEL_LEAKS
	if (writableOpenModelList != NULL)
		writableOpenModelList->RemoveItem(this);

	if (readOnlyOpenModelList != NULL)
		readOnlyOpenModelList->RemoveItem(this);
#endif

	if (fBaseType == kUnknownNode)
		SetupBaseType();

	switch (fBaseType) {
		case kExecutableNode:
		case kPlainNode:
		case kQueryNode:
		case kQueryTemplateNode:
		case kVirtualDirectoryNode:
			// open or reopen
			delete fNode;
			fNode = new BFile(&fEntryRef,
				(uint32)(writable ? O_RDWR : O_RDONLY));
			break;

		case kDesktopNode:
		case kDirectoryNode:
		case kRootNode:
		case kTrashNode:
		case kVolumeNode:
			if (!IsNodeOpen())
				fNode = new BDirectory(&fEntryRef);

			if (fBaseType == kDirectoryNode
				&& static_cast<BDirectory*>(fNode)->IsRootDirectory()) {
				// promote from directory to volume
				fBaseType = kVolumeNode;
			}
			break;

		case kLinkNode:
			if (!IsNodeOpen()) {
				BEntry entry(&fEntryRef);
				fNode = new BSymLink(&entry);
			}
			break;

		default:
#if DEBUG
			PrintToStream();
#endif
			TRESPASS();
				// this can only happen if GetStat failed before,
				// in which case we shouldn't be here

			// ToDo: Obviously, we can also be here if the type could not
			// be determined, for example for block devices (so the TRESPASS()
			// macro shouldn't be used here)!
			return fStatus = B_ERROR;
	}

	fStatus = fNode->InitCheck();
	if (fStatus != B_OK) {
		delete fNode;
		fNode = NULL;
		// original code snoozed an error here and returned B_OK
		return fStatus;
	}

	fWritable = writable;

	if (fMimeType.Length() <= 0)
		FinishSettingUpType();

#ifdef CHECK_OPEN_MODEL_LEAKS
	if (fWritable) {
		if (!writableOpenModelList) {
			TRACE();
			writableOpenModelList = new BObjectList<Model>(100);
		}
		writableOpenModelList->AddItem(this);
	} else {
		if (!readOnlyOpenModelList) {
			TRACE();
			readOnlyOpenModelList = new BObjectList<Model>(100);
		}
		readOnlyOpenModelList->AddItem(this);
	}
#endif

	if (gLocalizedNamePreferred)
		CacheLocalizedName();

	return fStatus;
}


void
Model::CloseNode()
{
#if xDEBUG
	PRINT(("closing node for %s\n", Name()));
#endif

#ifdef CHECK_OPEN_MODEL_LEAKS
	if (writableOpenModelList != NULL)
		writableOpenModelList->RemoveItem(this);

	if (readOnlyOpenModelList != NULL)
		readOnlyOpenModelList->RemoveItem(this);
#endif

	delete fNode;
	fNode = NULL;
}


bool
Model::IsNodeOpen() const
{
	return fNode != NULL;
}



bool
Model::IsNodeOpenForWriting() const
{
	return fNode != NULL && fWritable;
}


void
Model::SetupBaseType()
{
	switch (fStatBuf.st_mode & S_IFMT) {
		case S_IFDIR:
			// folder
			fBaseType = kDirectoryNode;
			break;

		case S_IFREG:
			// regular file
			if ((fStatBuf.st_mode & S_IXUSR) != 0) {
				// executable
				fBaseType = kExecutableNode;
			} else {
				// non-executable
				fBaseType = kPlainNode;
			}
			break;

		case S_IFLNK:
			// symlink
			fBaseType = kLinkNode;
			break;

		default:
			fBaseType = kUnknownNode;
			break;
	}
}


void
Model::CacheLocalizedName()
{
	if (!fLocalizedNameIsCached) {
		fLocalizedNameIsCached = true;
		if (BLocaleRoster::Default()->GetLocalizedFileName(
				fLocalizedName, fEntryRef, true) == B_OK)
			fHasLocalizedName = true;
		else
			fHasLocalizedName = false;
	}
}


void
Model::FinishSettingUpType()
{
	char type[B_MIME_TYPE_LENGTH];
	BEntry entry;

	// While we are reading the node, do a little snooping to see if it even
	// makes sense to look for a node-based icon. This serves as a hint to the
	// icon cache, allowing it to not hit the disk again for models that do not
	// have an icon defined by the node.
	if (fBaseType != kLinkNode && !CheckAppIconHint())
		fIconFrom = kUnknownNotFromNode;

	if (fBaseType != kDirectoryNode
		&& fBaseType != kVolumeNode
		&& fBaseType != kLinkNode
		&& IsNodeOpen()) {
		BNodeInfo info(fNode);

		// check if a specific mime type is set
		if (info.GetType(type) == B_OK) {
			// node has a specific mime type
			fMimeType = type;
			if (strcmp(type, B_QUERY_MIMETYPE) == 0)
				fBaseType = kQueryNode;
			else if (strcmp(type, B_QUERY_TEMPLATE_MIMETYPE) == 0)
				fBaseType = kQueryTemplateNode;
			else if (strcmp(type, kVirtualDirectoryMimeType) == 0)
				fBaseType = kVirtualDirectoryNode;

			attr_info thumb;
			if (fNode->GetAttrInfo(kAttrThumbnail, &thumb) == B_OK
				|| ShouldGenerateThumbnail(type)) {
				fIconFrom = kNode;
			}

			if (info.GetPreferredApp(type) == B_OK) {
				if (fPreferredAppName)
					DeletePreferredAppVolumeNameLinkTo();

				if (*type != '\0')
					fPreferredAppName = strdup(type);
			}
		}
	}

	switch (fBaseType) {
		case kDirectoryNode:
			entry.SetTo(&fEntryRef);
			if (entry.InitCheck() == B_OK) {
				if (FSIsTrashDir(&entry))
					fBaseType = kTrashNode;
				else if (FSIsDeskDir(&entry))
					fBaseType = kDesktopNode;
			}

			fMimeType = B_DIR_MIMETYPE;
				// should use a shared string here
			if (IsNodeOpen()) {
				BNodeInfo info(fNode);
				if (info.GetType(type) == B_OK)
					fMimeType = type;

				if (fIconFrom == kUnknownNotFromNode
					&& WellKnowEntryList::Match(NodeRef())
						> (directory_which)-1) {
					// one of home, beos, system, boot, etc.
					fIconFrom = kTrackerSupplied;
				}
			}
			break;

		case kVolumeNode:
		{
			if (NodeRef()->node == fEntryRef.directory
				&& NodeRef()->device == fEntryRef.device) {
				// promote from volume to file system root
				fBaseType = kRootNode;
				fMimeType = B_ROOT_MIMETYPE;
				break;
			}

			// volumes have to have a B_VOLUME_MIMETYPE type
			fMimeType = B_VOLUME_MIMETYPE;
			if (fIconFrom == kUnknownNotFromNode) {
				if (WellKnowEntryList::Match(NodeRef()) > (directory_which)-1)
					fIconFrom = kTrackerSupplied;
				else
					fIconFrom = kVolume;
			}

			char name[B_FILE_NAME_LENGTH];
			BVolume volume(NodeRef()->device);
			if (volume.InitCheck() == B_OK && volume.GetName(name) == B_OK) {
				if (fVolumeName != NULL)
					DeletePreferredAppVolumeNameLinkTo();

				fVolumeName = strdup(name);
			}
#if DEBUG
			else
				PRINT(("get volume name failed for %s\n", fEntryRef.name));
#endif
			break;
		}

		case kLinkNode:
			fMimeType = B_LINK_MIMETYPE;
				// should use a shared string here
			break;

		case kExecutableNode:
			if (IsNodeOpen()) {
				char signature[B_MIME_TYPE_LENGTH];
				if (GetAppSignatureFromAttr(dynamic_cast<BFile*>(fNode),
						signature) == B_OK) {
					if (fPreferredAppName)
						DeletePreferredAppVolumeNameLinkTo();

					if (signature[0])
						fPreferredAppName = strdup(signature);
				}
			}
			if (fMimeType.Length() <= 0)
				fMimeType = B_APP_MIME_TYPE;
					// should use a shared string here
			break;

		default:
			if (fMimeType.Length() <= 0)
				fMimeType = B_FILE_MIMETYPE;
			break;
	}
}


bool
Model::ShouldUseWellKnownIcon() const
{
	if (fBaseType == kDirectoryNode || fBaseType == kVolumeNode
		|| fBaseType == kTrashNode || fBaseType == kDesktopNode)
		return !CheckAppIconHint();
	return false;
}


bool
Model::CheckAppIconHint() const
{
	attr_info info;
	if (fNode == NULL) {
		// Node is not open.
		return false;
	}

	if (fNode->GetAttrInfo(kAttrIcon, &info) == B_OK) {
		// Node has a vector icon
		return true;
	}

	if (fNode->GetAttrInfo(kAttrMiniIcon, &info) == B_OK
		&& fNode->GetAttrInfo(kAttrLargeIcon, &info) == B_OK) {
		// Node has a mini _and_ large icon
		return true;
	}

	// If there isn't either of these, we can't use the icon attribute from the node.
	return false;
}


void
Model::ResetIconFrom()
{
	BModelOpener opener(this);

	if (InitCheck() != B_OK)
		return;

	if (ShouldUseWellKnownIcon()) {
		BDirectory* directory = dynamic_cast<BDirectory*>(fNode);
		if (WellKnowEntryList::Match(NodeRef()) > (directory_which)-1) {
			fIconFrom = kTrackerSupplied;
			return;
		} else if (directory != NULL && directory->IsRootDirectory()) {
			fIconFrom = kVolume;
			return;
		}
	}
	fIconFrom = kUnknownSource;
}


const char*
Model::PreferredAppSignature() const
{
	if (IsVolume() || IsSymLink())
		return "";

	return fPreferredAppName ? fPreferredAppName : "";
}


void
Model::SetPreferredAppSignature(const char* signature)
{
	ASSERT(!IsVolume() && !IsSymLink());
	ASSERT(signature != fPreferredAppName);
		// self assignment should not be an option

	free(fPreferredAppName);
	if (signature)
		fPreferredAppName = strdup(signature);
	else
		fPreferredAppName = NULL;
}


bool
Model::IsPrintersDir() const
{
	BEntry entry(EntryRef());
	return FSIsPrintersDir(&entry);
}


bool
Model::InRoot() const
{
	return FSInRootDir(EntryRef());
}


bool
Model::InTrash() const
{
	return FSInTrashDir(EntryRef());
}


const Model*
Model::ResolveIfLink() const
{
	if (!IsSymLink())
		return this;

	if (!fLinkTo)
		return this;

	return fLinkTo;
}


Model*
Model::ResolveIfLink()
{
	if (!IsSymLink())
		return this;

	if (!fLinkTo)
		return this;

	return fLinkTo;
}


void
Model::SetLinkTo(Model* model)
{
	ASSERT(IsSymLink());
	ASSERT(!fLinkTo || (fLinkTo != model));

	delete fLinkTo;
	fLinkTo = model;
}


//	#pragma mark - Node monitor updating methods


void
Model::UpdateEntryRef(const node_ref* dirNode, const char* name)
{
	if (IsVolume()) {
		if (fVolumeName != NULL)
			DeletePreferredAppVolumeNameLinkTo();

		fVolumeName = strdup(name);
	}

	fEntryRef.device = dirNode->device;
	fEntryRef.directory = dirNode->node;

	if (fEntryRef.name != NULL && strcmp(fEntryRef.name, name) == 0)
		return;

	fEntryRef.set_name(name);
}


status_t
Model::WatchVolumeAndMountPoint(uint32 , BHandler* target)
{
	ASSERT(IsVolume());

	if (fEntryRef.name != NULL && fVolumeName != NULL
		&& strcmp(fEntryRef.name, "boot") == 0) {
		// watch mount point for boot volume
		BString bootMountPoint("/");
		bootMountPoint += fVolumeName;
		BEntry mountPointEntry(bootMountPoint.String());
		Model mountPointModel(&mountPointEntry);

		TTracker::WatchNode(mountPointModel.NodeRef(),
			B_WATCH_NAME | B_WATCH_STAT | B_WATCH_ATTR, target);
	}

	return TTracker::WatchNode(NodeRef(),
		B_WATCH_NAME | B_WATCH_STAT | B_WATCH_ATTR, target);
}


bool
Model::AttrChanged(const char* attrName)
{
	// called on an attribute changed node monitor
	// sync up cached values of mime type and preferred app and
	// return true if icon needs updating

	ASSERT(IsNodeOpen());
	if (attrName != NULL
		&& (strcmp(attrName, kAttrIcon) == 0
			|| strcmp(attrName, kAttrMiniIcon) == 0
			|| strcmp(attrName, kAttrLargeIcon) == 0
			|| strcmp(attrName, kAttrThumbnail) == 0)) {
		return true;
	}

	if (attrName == NULL
		|| strcmp(attrName, kAttrMIMEType) == 0
		|| strcmp(attrName, kAttrPreferredApp) == 0) {
		char type[B_MIME_TYPE_LENGTH];
		BNodeInfo info(fNode);
		if (info.GetType(type) != B_OK)
			fMimeType = "";
		else {
			// node has a specific mime type
			fMimeType = type;
			if (!IsVolume() && !IsSymLink()
				&& info.GetPreferredApp(type) == B_OK) {
				SetPreferredAppSignature(type);
			}
		}

#if xDEBUG
		if (fIconFrom != kNode) {
			PRINT(("%s, %s:updating icon because file type changed\n",
				Name(), attrName != NULL ? attrName : ""));
		} else {
			PRINT(("Not updating icon even though type changed "
				"because icon is from node.\n"));
		}
#endif

		return fIconFrom != kNode;
			// update icon unless it is coming from a node
	}

	return attrName == NULL;
}


bool
Model::StatChanged()
{
	ASSERT(IsNodeOpen());
	mode_t oldMode = fStatBuf.st_mode;
	fStatus = fNode->GetStat(&fStatBuf);

	if (oldMode != fStatBuf.st_mode) {
		bool forWriting = IsNodeOpenForWriting();
		CloseNode();
		//SetupBaseType();
			// the node type can't change with a stat update...
		OpenNodeCommon(forWriting);
		return true;
	}

	return false;
}


//	#pragma mark - Mime handling methods


bool
Model::IsDropTarget(const Model* forDocument, bool traverse) const
{
	switch (CanHandleDrops()) {
		case kCanHandle:
			return true;

		case kCannotHandle:
			return false;

		default:
			break;
	}

	if (forDocument == NULL)
		return true;

	if (traverse) {
		BEntry entry(forDocument->EntryRef(), true);
		if (entry.InitCheck() != B_OK)
			return false;

		BFile file(&entry, O_RDONLY);
		BNodeInfo mime(&file);

		if (mime.InitCheck() != B_OK)
			return false;

		char mimeType[B_MIME_TYPE_LENGTH];
		mime.GetType(mimeType);

		return SupportsMimeType(mimeType, 0) != kDoesNotSupportType;
	}

	// do some mime-based matching
	const char* documentMimeType = forDocument->MimeType();
	if (documentMimeType == NULL)
		return false;

	return SupportsMimeType(documentMimeType, 0) != kDoesNotSupportType;
}


Model::CanHandleResult
Model::CanHandleDrops() const
{
	if (IsDirectory() || IsVirtualDirectory()) {
		// directories take anything
		// resolve permissions here
		return kCanHandle;
	}

	if (IsSymLink()) {
		// descend into symlink and try again on it's target

		BEntry entry(&fEntryRef, true);
		if (entry.InitCheck() != B_OK)
			return kCannotHandle;

		if (entry == BEntry(EntryRef()))
			// self-referencing link, avoid infinite recursion
			return kCannotHandle;

		Model model(&entry);
		if (model.InitCheck() != B_OK)
			return kCannotHandle;

		return model.CanHandleDrops();
	}

	if (IsExecutable())
		return kNeedToCheckType;

	return kCannotHandle;
}


inline bool
IsSuperHandlerSignature(const char* signature)
{
	return strcasecmp(signature, B_FILE_MIMETYPE) == 0;
}


enum {
	kDontMatch = 0,
	kMatchSupertype,
	kMatch
};


static int32
MatchMimeTypeString(const BString& documentType, const char* handlerType)
{
	// perform a mime type wildcard match
	// handler types of the form "text"
	// handle every handled type with same supertype,
	// for everything else a full string match is used

	int32 supertypeOnlyLength = 0;
	const char* tmp = strstr(handlerType, "/");

	if (tmp == NULL) {
		// no subtype - supertype string only
		supertypeOnlyLength = (int32)strlen(handlerType);
	}

	if (supertypeOnlyLength) {
		// compare just the supertype
		tmp = strstr(documentType.String(), "/");
		if (tmp && (tmp - documentType.String() == supertypeOnlyLength)) {
			if (documentType.ICompare(handlerType, supertypeOnlyLength) == 0)
				return kMatchSupertype;
			else
				return kDontMatch;
		}
	}

	if (documentType.ICompare(handlerType) == 0)
		return kMatch;

	return kDontMatch;
}


int32
Model::SupportsMimeType(const char* type, const BStringList* list,
	bool exactReason) const
{
	ASSERT((type == 0) != (list == 0));
		// pass in one or the other

	int32 result = kDoesNotSupportType;

	BFile file(EntryRef(), O_RDONLY);
	BAppFileInfo handlerInfo(&file);

	BMessage message;
	if (handlerInfo.GetSupportedTypes(&message) != B_OK)
		return kDoesNotSupportType;

	for (int32 index = 0; ; index++) {
		// check if this model lists the type of dropped document as supported

		const char* mimeSignature;
		ssize_t bufferLength;

		if (message.FindData("types", 'CSTR', index,
				(const void**)&mimeSignature, &bufferLength)) {
			return result;
		}

		if (IsSuperHandlerSignature(mimeSignature)) {
			if (!exactReason)
				return kSuperhandlerModel;

			if (result == kDoesNotSupportType)
				result = kSuperhandlerModel;
		}

		int32 match = kDontMatch;

		if (type != NULL || (list != NULL && list->IsEmpty())) {
			BString typeString(type);
			match = MatchMimeTypeString(typeString, mimeSignature);
		} else {
			const int32 count = list->CountStrings();
			for (int32 i = 0; i < count; i++) {
				match = MatchMimeTypeString(list->StringAt(i), mimeSignature);
				if (match != kDontMatch)
					break;
			}
		}

		if (match == kMatch) {
			// supports the actual type, it can't get any better
			return kModelSupportsType;
		} else if (match == kMatchSupertype) {
			if (!exactReason)
				return kModelSupportsSupertype;

			// we already know this model supports the file as a supertype,
			// now find out if it matches the type
			result = kModelSupportsSupertype;
		}
	}

	return result;
}


bool
Model::IsDropTargetForList(const BStringList* list) const
{
	switch (CanHandleDrops()) {
		case kCanHandle:
			return true;

		case kCannotHandle:
			return false;

		default:
			break;
	}

	return SupportsMimeType(0, list) != kDoesNotSupportType;
}


bool
Model::IsSuperHandler() const
{
	ASSERT(CanHandleDrops() == kNeedToCheckType);

	BFile file(EntryRef(), O_RDONLY);
	BAppFileInfo handlerInfo(&file);

	BMessage message;
	if (handlerInfo.GetSupportedTypes(&message) != B_OK)
		return false;

	for (int32 index = 0; ; index++) {
		const char* mimeSignature;
		ssize_t bufferLength;

		if (message.FindData("types", 'CSTR', index,
			(const void**)&mimeSignature, &bufferLength)) {
			return false;
		}

		if (IsSuperHandlerSignature(mimeSignature))
			return true;
	}
	return false;
}


void
Model::GetEntry(BEntry* entry) const
{
	entry->SetTo(EntryRef());
}


void
Model::GetPath(BPath* path) const
{
	BEntry entry(EntryRef());
	entry.GetPath(path);
}


bool
Model::Mimeset(bool force)
{
	BString oldType = MimeType();
	BPath path;
	GetPath(&path);

	update_mime_info(path.Path(), 0, 1, force ? 2 : 0);
	ModelNodeLazyOpener opener(this);
	opener.OpenNode();
	AttrChanged(NULL);

	return !oldType.ICompare(MimeType());
}


ssize_t
Model::WriteAttr(const char* attr, type_code type, off_t offset,
	const void* buffer, size_t length)
{
	BModelWriteOpener opener(this);
	if (!fNode)
		return 0;

	ssize_t result = fNode->WriteAttr(attr, type, offset, buffer, length);
	return result;
}


ssize_t
Model::WriteAttrKillForeign(const char* attr, const char* foreignAttr,
	type_code type, off_t offset, const void* buffer, size_t length)
{
	BModelWriteOpener opener(this);
	if (!fNode)
		return 0;

	ssize_t result = fNode->WriteAttr(attr, type, offset, buffer, length);
	if (result == (ssize_t)length)
		// nuke attribute in opposite endianness
		fNode->RemoveAttr(foreignAttr);
	return result;
}


status_t
Model::GetLongVersionString(BString &result, version_kind kind)
{
	BFile file(EntryRef(), O_RDONLY);
	status_t error = file.InitCheck();
	if (error != B_OK)
		return error;

	BAppFileInfo info(&file);
	error = info.InitCheck();
	if (error != B_OK)
		return error;

	version_info version;
	error = info.GetVersionInfo(&version, kind);
	if (error != B_OK)
		return error;

	result = version.long_info;
	return B_OK;
}


status_t
Model::GetVersionString(BString &result, version_kind kind)
{
	BFile file(EntryRef(), O_RDONLY);
	status_t error = file.InitCheck();
	if (error != B_OK)
		return error;

	BAppFileInfo info(&file);
	error = info.InitCheck();
	if (error != B_OK)
		return error;

	version_info version;
	error = info.GetVersionInfo(&version, kind);
	if (error != B_OK)
		return error;

	result.SetToFormat("%" B_PRId32 ".%" B_PRId32 ".%" B_PRId32, version.major,
		version.middle, version.minor);

	return B_OK;
}

#if DEBUG

void
Model::PrintToStream(int32 level, bool deep)
{
	PRINT(("model name %s, entry name %s, inode %" B_PRIdINO ", dev %"
		B_PRIdDEV ", directory inode %" B_PRIdINO "\n",
		Name() ? Name() : "**empty name**",
		EntryRef()->name ? EntryRef()->name : "**empty ref name**",
		NodeRef()->node,
		NodeRef()->device,
		EntryRef()->directory));
	PRINT(("type %s \n", MimeType()));

	PRINT(("model type: "));
	switch (fBaseType) {
		case kPlainNode:
			PRINT(("plain\n"));
			break;

		case kQueryNode:
			PRINT(("query\n"));
			break;

		case kQueryTemplateNode:
			PRINT(("query template\n"));
			break;

		case kExecutableNode:
			PRINT(("exe\n"));
			break;

		case kDirectoryNode:
		case kTrashNode:
		case kDesktopNode:
			PRINT(("dir\n"));
			break;

		case kLinkNode:
			PRINT(("link\n"));
			break;

		case kRootNode:
			PRINT(("root\n"));
			break;

		case kVolumeNode:
			PRINT(("volume, name %s\n", fVolumeName ? fVolumeName : ""));
			break;

		case kVirtualDirectoryNode:
			PRINT(("virtual directory\n"));
			break;

		default:
			PRINT(("unknown\n"));
			break;
	}

	if (level < 1)
		return;

	if (!IsVolume()) {
		PRINT(("preferred app %s\n",
			fPreferredAppName ? fPreferredAppName : ""));
	}

	PRINT(("icon from: "));
	switch (IconFrom()) {
		case kUnknownSource:
			PRINT(("unknown\n"));
			break;

		case kUnknownNotFromNode:
			PRINT(("unknown but not from a node\n"));
			break;

		case kTrackerDefault:
			PRINT(("tracker default\n"));
			break;

		case kTrackerSupplied:
			PRINT(("tracker supplied\n"));
			break;

		case kMetaMime:
			PRINT(("metamime\n"));
			break;

		case kPreferredAppForType:
			PRINT(("preferred app for type\n"));
			break;

		case kPreferredAppForNode:
			PRINT(("preferred app for node\n"));
			break;

		case kNode:
			PRINT(("node\n"));
			break;

		case kVolume:
			PRINT(("volume\n"));
			break;

		default:
			break;
	}

	PRINT(("model %s opened %s \n", !IsNodeOpen() ? "not " : "",
		IsNodeOpenForWriting() ? "for writing" : ""));

	if (IsNodeOpen()) {
		node_ref nodeRef;
		fNode->GetNodeRef(&nodeRef);
		PRINT(("node ref of open Node %" B_PRIdINO " %" B_PRIdDEV "\n",
			nodeRef.node, nodeRef.device));
	}

	if (deep && IsSymLink()) {
		BEntry tmpEntry(EntryRef(), true);
		Model tmp(&tmpEntry);
		PRINT(("symlink to:\n"));
		tmp.PrintToStream();
	}
	TrackIconSource(B_MINI_ICON);
	TrackIconSource(B_LARGE_ICON);
}


void
Model::TrackIconSource(icon_size size)
{
	PRINT(("tracking %s icon\n", size == B_LARGE_ICON ? "large" : "small"));
	BRect rect;
	if (size == B_MINI_ICON)
		rect.Set(0, 0, B_MINI_ICON - 1, B_MINI_ICON - 1);
	else
		rect.Set(0, 0, B_LARGE_ICON - 1, B_LARGE_ICON - 1);

	BBitmap bitmap(rect, B_CMAP8);

	BModelOpener opener(this);

	if (Node() == NULL) {
		PRINT(("track icon error - no node\n"));
		return;
	}

	if (IsSymLink()) {
		PRINT(("tracking symlink icon\n"));
		if (fLinkTo) {
			fLinkTo->TrackIconSource(size);
			return;
		}
	}

	if (fBaseType == kVolumeNode) {
		BVolume volume(NodeRef()->device);
		status_t result = volume.GetIcon(&bitmap, size);
		PRINT(("getting icon from volume %s\n", strerror(result)));
	} else {
		BNodeInfo nodeInfo(Node());

		status_t err = nodeInfo.GetIcon(&bitmap, size);
		if (err == B_OK) {
			// file knew which icon to use, we are done
			PRINT(("track icon - got icon from file\n"));
			return;
		}

		char preferredApp[B_MIME_TYPE_LENGTH];
		err = nodeInfo.GetPreferredApp(preferredApp);
		if (err == B_OK && preferredApp[0]) {
			BMimeType preferredAppType(preferredApp);
			err = preferredAppType.GetIconForType(MimeType(), &bitmap, size);
			if (err == B_OK) {
				PRINT(
					("track icon - got icon for type %s from preferred "
					 "app %s for file\n", MimeType(), preferredApp));
				return;
			}
		}

		BMimeType mimeType(MimeType());
		err = mimeType.GetIcon(&bitmap, size);
		if (err == B_OK) {
			// the system knew what icon to use for the type, we are done
			PRINT(("track icon - signature %s, got icon from system\n",
				MimeType()));
			return;
		}

		err = mimeType.GetPreferredApp(preferredApp);
		if (err != B_OK) {
			// no preferred App for document, give up
			PRINT(("track icon - signature %s, no prefered app, error %s\n",
				MimeType(), strerror(err)));
			return;
		}

		BMimeType preferredAppType(preferredApp);
		err = preferredAppType.GetIconForType(MimeType(), &bitmap, size);
		if (err == B_OK) {
			// the preferred app knew icon to use for the type, we are done
			PRINT(
				("track icon - signature %s, got icon from preferred "
				 "app %s\n", MimeType(), preferredApp));
			return;
		}
		PRINT(
			("track icon - signature %s, preferred app %s, no icon, "
			 "error %s\n", MimeType(), preferredApp, strerror(err)));
	}
}

#endif	// DEBUG

#ifdef CHECK_OPEN_MODEL_LEAKS

namespace BPrivate {

#include <stdio.h>

void
DumpOpenModels(bool extensive)
{
	if (readOnlyOpenModelList) {
		int32 count = readOnlyOpenModelList->CountItems();
		printf("%ld models open read-only:\n", count);
		printf("==========================\n");
		for (int32 index = 0; index < count; index++) {
			if (extensive) {
				printf("---------------------------\n");
				readOnlyOpenModelList->ItemAt(index)->PrintToStream();
			} else
				printf("%s\n", readOnlyOpenModelList->ItemAt(index)->Name());
		}
	}

	if (writableOpenModelList) {
		int32 count = writableOpenModelList->CountItems();
		printf("%ld models open writable:\n", count);
		printf("models open writable:\n");
		printf("======================\n");
		for (int32 index = 0; index < count; index++) {
			if (extensive) {
				printf("---------------------------\n");
				writableOpenModelList->ItemAt(index)->PrintToStream();
			} else
				printf("%s\n", writableOpenModelList->ItemAt(index)->Name());
		}
	}
}


void
InitOpenModelDumping()
{
	readOnlyOpenModelList = 0;
	writableOpenModelList = 0;
}

}	// namespace BPrivate

#endif	// CHECK_OPEN_MODEL_LEAKS
