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


#include "FindPanel.h"

#include <utility>

#include <errno.h>
#include <fs_attr.h>
#include <parsedate.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Cursor.h>
#include <Debug.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Query.h>
#include <SeparatorView.h>
#include <Size.h>
#include <SpaceLayoutItem.h>
#include <StringFormat.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "Attributes.h"
#include "AutoLock.h"
#include "Commands.h"
#include "ContainerWindow.h"
#include "FSUtils.h"
#include "FunctionObject.h"
#include "IconMenuItem.h"
#include "MimeTypes.h"
#include "Tracker.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FindPanel"


const char* kAllMimeTypes = "mime/ALLTYPES";

const uint32 kNameModifiedMessage = 'nmmd';
const uint32 kSwitchToQueryTemplate = 'swqt';
const uint32 kRunSaveAsTemplatePanel = 'svtm';
const uint32 kLatchChanged = 'ltch';

static const float kPopUpIndicatorWidth = 13.0f;

const char* kDragNDropTypes[] = {
	B_QUERY_MIMETYPE,
	B_QUERY_TEMPLATE_MIMETYPE
};
static const char* kDragNDropActionSpecifiers[] = {
	B_TRANSLATE_MARK("Create a Query"),
	B_TRANSLATE_MARK("Create a Query template")
};

const uint32 kAttachFile = 'attf';

const int32 operators[] = {
	B_CONTAINS,
	B_EQ,
	B_NE,
	B_BEGINS_WITH,
	B_ENDS_WITH,
	B_GT,
	B_LT
};

static const char* operatorLabels[] = {
	B_TRANSLATE_MARK("contains"),
	B_TRANSLATE_MARK("is"),
	B_TRANSLATE_MARK("is not"),
	B_TRANSLATE_MARK("starts with"),
	B_TRANSLATE_MARK("ends with"),
	B_TRANSLATE_MARK("greater than"),
	B_TRANSLATE_MARK("less than"),
	B_TRANSLATE_MARK("before"),
	B_TRANSLATE_MARK("after")
};

namespace BPrivate {

class MostUsedNames {
public:
								MostUsedNames(const char* fileName, const char* directory,
									int32 maxCount = 5);
								~MostUsedNames();

			bool				ObtainList(BStringList* list);
			void				ReleaseList();

			void 				AddName(const BString&);

protected:
			struct list_entry {
				BString name;
				int32 count;
			};

		static int CompareNames(const list_entry* a, const list_entry* b);
		void LoadList();
		void UpdateList();

		const char*	fFileName;
		const char*	fDirectory;
		bool		fLoaded;
		mutable Benaphore fLock;
		BObjectList<list_entry> fList;
		int32		fCount;
};


MostUsedNames gMostUsedMimeTypes("MostUsedMimeTypes", "Tracker");


//	#pragma mark - MoreOptionsStruct


void
MoreOptionsStruct::EndianSwap(void*)
{
	// noop for now
}


void
MoreOptionsStruct::SetQueryTemporary(BNode* node, bool on)
{
	MoreOptionsStruct saveMoreOptions;

	if (ReadAttr(node, kAttrQueryMoreOptions, kAttrQueryMoreOptionsForeign,
			B_RAW_TYPE, 0, &saveMoreOptions, sizeof(MoreOptionsStruct),
			&MoreOptionsStruct::EndianSwap) == B_OK) {
		saveMoreOptions.temporary = on;
		node->WriteAttr(kAttrQueryMoreOptions, B_RAW_TYPE, 0, &saveMoreOptions,
			sizeof(saveMoreOptions));
	}
}


bool
MoreOptionsStruct::QueryTemporary(const BNode* node)
{
	MoreOptionsStruct saveMoreOptions;

	if (ReadAttr(node, kAttrQueryMoreOptions, kAttrQueryMoreOptionsForeign,
		B_RAW_TYPE, 0, &saveMoreOptions, sizeof(MoreOptionsStruct),
		&MoreOptionsStruct::EndianSwap) == kReadAttrFailed) {
		return false;
	}

	return saveMoreOptions.temporary;
}


//	#pragma mark - FindWindow


FindWindow::FindWindow(const entry_ref* newRef, bool editIfTemplateOnly)
	:
	BWindow(BRect(), B_TRANSLATE("Find"), B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE | B_AUTO_UPDATE_SIZE_LIMITS),
	fFile(TryOpening(newRef)),
	fFromTemplate(false),
	fEditTemplateOnly(false),
	fSaveAsPanel(NULL),
	fOpenQueryPanel(NULL),
	fSaveQueryOrTemplateItem(new BMenuItem(B_TRANSLATE("Save"),
		new BMessage(kSaveQueryOrTemplate), 'S'))
{
	if (fFile != NULL) {
		fRef = *newRef;
		if (editIfTemplateOnly) {
			char type[B_MIME_TYPE_LENGTH];
			if (BNodeInfo(fFile).GetType(type) == B_OK
				&& strcasecmp(type, B_QUERY_TEMPLATE_MIMETYPE) == 0) {
				fEditTemplateOnly = true;
				SetTitle(B_TRANSLATE("Edit Query template"));
			}
		}
	} else {
		// no initial query, fall back on the default query template
		BDirectory directory(GetQueriesDirectory().Path());
		BEntry entry(&directory, "default");

		entry_ref defaultRef;
		if (entry.Exists() && entry.GetRef(&defaultRef) == B_OK)
			fFile = TryOpening(&defaultRef);
		else {
			// no default query template yet
			fFile = new BFile(&entry, O_RDWR | O_CREAT);
			if (fFile->InitCheck() < B_OK) {
				delete fFile;
				fFile = NULL;
			} else
				SaveQueryAttributes(fFile, true);
		}

		fSaveQueryOrTemplateItem->SetEnabled(false);
	}

	fFromTemplate = IsQueryTemplate(fFile);

	fBackground = new FindPanel(fFile, this, fFromTemplate,
		fEditTemplateOnly);

	BuildMenuBar();

	BGroupLayout* layout = new BGroupLayout(B_VERTICAL);
	SetLayout(layout);
	layout->SetSpacing(0);
	layout->SetInsets(0, 0, 0, 0);

	GetLayout()->AddView(fMenuBar);
	GetLayout()->AddView(fBackground);
	CenterOnScreen();
}


FindWindow::~FindWindow()
{
	delete fFile;
	delete fSaveAsPanel;
	delete fOpenQueryPanel;
}


void
FindWindow::BuildMenuBar()
{
	fMenuBar = new BMenuBar("Menu Bar");

	fQueryMenu = new BMenu(B_TRANSLATE("Query"));
	fOptionsMenu = new BMenu(B_TRANSLATE("Options"));
	fTemplatesMenu = new BMenu(B_TRANSLATE("Templates"));

	fHistoryMenu = new BMenu(B_TRANSLATE("Recent queries"));
	BMessenger messenger(fBackground);
	FindPanel::AddRecentQueries(fHistoryMenu, false, &messenger, kSwitchToQueryTemplate, false);

	IconMenuItem* historyMenuItem = new IconMenuItem(fHistoryMenu,
		new BMessage(kOpenDir), B_DIR_MIMETYPE);

	BMenuItem* saveAsQueryItem = new BMenuItem(B_TRANSLATE("Save as query" B_UTF8_ELLIPSIS), NULL);
	BMessage* saveAsQueryMessage = new BMessage(kOpenSaveAsPanel);
	saveAsQueryMessage->AddBool("saveastemplate", false);
	saveAsQueryItem->SetMessage(saveAsQueryMessage);
	BMenuItem* saveAsQueryTemplateItem = new BMenuItem(B_TRANSLATE("Save as template"
			B_UTF8_ELLIPSIS),
		NULL);
	BMessage* saveAsQueryTemplateMessage = new BMessage(kOpenSaveAsPanel);
	saveAsQueryTemplateMessage->AddBool("saveastemplate", true);
	saveAsQueryTemplateItem->SetMessage(saveAsQueryTemplateMessage);

	fQueryMenu->AddItem(
		new BMenuItem(B_TRANSLATE("Open" B_UTF8_ELLIPSIS), new BMessage(kOpenLoadQueryPanel), 'O'));
	fQueryMenu->AddItem(fSaveQueryOrTemplateItem);
	fQueryMenu->AddItem(saveAsQueryItem);
	fQueryMenu->AddItem(saveAsQueryTemplateItem);
	fQueryMenu->AddSeparatorItem();
	fQueryMenu->AddItem(historyMenuItem);

	fSearchInTrash = new BMenuItem(
		B_TRANSLATE("Include Trash"), new BMessage(kSearchInTrashOptionClicked));
	fOptionsMenu->AddItem(fSearchInTrash);

	PopulateTemplatesMenu();

	fMenuBar->AddItem(fQueryMenu);
	fMenuBar->AddItem(fOptionsMenu);
	fMenuBar->AddItem(fTemplatesMenu);
}


void
FindWindow::UpdateFileReferences(const entry_ref* ref)
{
	if (ref == NULL) {
		fFile->Unset();
		fFile = NULL;
		fFromTemplate = false;
		fRef = entry_ref();
	}

	BEntry entry(ref);
	if (!entry.Exists())
		return;

	fFile->Unset();
	fFile = NULL;
	fFile = TryOpening(ref);

	if (fFile != NULL) {
		entry.GetRef(&fRef);
		fFromTemplate = IsQueryTemplate(fFile);
	}
}


status_t
FindWindow::DeleteQueryOrTemplate(BEntry* entry)
{
	// params checking
	if (entry == NULL)
		return B_BAD_VALUE;

	if (entry->Exists()) {
		entry_ref ref;
		entry->GetRef(&ref);
		if (fRef == ref) {
			UpdateFileReferences(NULL);
			fSaveQueryOrTemplateItem->SetEnabled(false);
		}
		entry->Remove();
		return B_OK;
	} else {
		return B_ENTRY_NOT_FOUND;
	}
}


static bool
CheckForDuplicates(BObjectList<entry_ref, true>* list, entry_ref* ref)
{
	// Simple Helper Function To Check For Duplicates Within an Entry List of Templates
	int32 count = list->CountItems();
	BPath comparison(ref);
	for (int32 i = 0; i < count; i++) {
		if (BPath(list->ItemAt(i)) == comparison)
			return true;
	}
	return false;
}


void
FindWindow::PopulateTemplatesMenu()
{
	fTemplatesMenu->RemoveItems(0, fTemplatesMenu->CountItems(), true);

	BObjectList<entry_ref, true> templates(10);
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsPersistent() && volume.KnowsQuery() && volume.KnowsAttr()) {
			BQuery query;
			query.SetVolume(&volume);
			query.SetPredicate("_trk/recentQuery == 1");

			if (query.Fetch() != B_OK)
				continue;

			entry_ref ref;
			while (query.GetNextRef(&ref) == B_OK) {
				if (FSInTrashDir(&ref))
					continue;

				char type[B_MIME_TYPE_LENGTH];
				BNode node(&ref);
				BNodeInfo(&node).GetType(type);
				if (strcmp(type, B_QUERY_TEMPLATE_MIMETYPE) == 0 && BEntry(&ref).Exists()
					&& CheckForDuplicates(&templates, &ref) == false) {
					// Checking for duplicates as BQuery returns multiple instances
					// of the same file if they are deleted at times.

					BMessage* message = new BMessage(kSwitchToQueryTemplate);
					message->AddRef("refs", &ref);
					BMenuItem* item = new IconMenuItem(ref.name, message, type);
					item->SetTarget(BMessenger(fBackground));
					fTemplatesMenu->AddItem(item);
					templates.AddItem(new entry_ref(ref));
				}
			}
		}
	}
}


void
FindWindow::SetOptions(bool searchInTrash)
{
	ASSERT(fSearchInTrash != NULL);
	fSearchInTrash->SetMarked(searchInTrash);
}


BFile*
FindWindow::TryOpening(const entry_ref* ref)
{
	if (!ref)
		return NULL;

	BFile* result = new BFile(ref, O_RDWR);
	if (result->InitCheck() != B_OK) {
		delete result;
		result = NULL;
	}
	return result;
}


BPath
FindWindow::GetQueriesDirectory()
{
	BPath path;
	if (find_directory(B_USER_DIRECTORY, &path, true) == B_OK
			&& path.Append("queries") == B_OK
			&& (mkdir(path.Path(), 0777) == 0 || errno == EEXIST)) {
		return path;
	}
	return BPath();
}


bool
FindWindow::IsQueryTemplate(BNode* file)
{
	char type[B_MIME_TYPE_LENGTH];
	if (BNodeInfo(file).GetType(type) != B_OK)
		return false;

	return strcasecmp(type, B_QUERY_TEMPLATE_MIMETYPE) == 0;
}


void
FindWindow::SwitchToTemplate(const entry_ref* ref)
{
	try {
		BEntry entry(ref, true);
		BFile templateFile(&entry, O_RDONLY);

		ThrowOnInitCheckError(&templateFile);
		fBackground->SwitchToTemplate(&templateFile);
	} catch (...) {
		;
	}
}


const char*
FindWindow::QueryName() const
{
	if (fFromTemplate) {
		if (!fQueryNameFromTemplate.Length() && fFile != NULL) {
			fFile->ReadAttrString(kAttrQueryTemplateName,
				&fQueryNameFromTemplate);
		}

		return fQueryNameFromTemplate.String();
	}
	if (fFile == NULL)
		return "";

	return fRef.name;
}


static const char*
MakeValidFilename(BString& string)
{
	// make a file name that is legal under bfs and hfs - possibly could
	// add code here to accomodate FAT32 etc. too
	if (string.Length() > B_FILE_NAME_LENGTH - 1) {
		string.Truncate(B_FILE_NAME_LENGTH - 4);
		string += B_UTF8_ELLIPSIS;
	}

	// replace slashes
	int32 length = string.Length();
	char* buf = string.LockBuffer(length);
	for (int32 index = length; index-- > 0;) {
		if (buf[index] == '/' /*|| buf[index] == ':'*/)
			buf[index] = '_';
	}
	string.UnlockBuffer(length);

	return string.String();
}


void
FindWindow::GetPredicateString(BString& predicate, bool& dynamicDate)
{
	BQuery query;
	switch (fBackground->Mode()) {
		case kByNameItem:
			fBackground->GetByNamePredicate(&query);
			query.GetPredicate(&predicate);
			break;

		case kByFormulaItem:
		{
			BTextControl* textControl
				= dynamic_cast<BTextControl*>(FindView("TextControl"));
			if (textControl != NULL)
				predicate.SetTo(textControl->Text(), 1023);
			break;
		}

		case kByAttributeItem:
			fBackground->GetByAttrPredicate(&query, dynamicDate);
			query.GetPredicate(&predicate);
			break;
	}
}


void
FindWindow::GetDefaultName(BString& name)
{
	fBackground->GetDefaultName(name);

	time_t timeValue = time(0);
	char namebuf[B_FILE_NAME_LENGTH];

	tm timeData;
	localtime_r(&timeValue, &timeData);

	strftime(namebuf, 32, " - %b %d, %I:%M:%S %p", &timeData);
	name << namebuf;

	MakeValidFilename(name);
}


void
FindWindow::SaveQueryAttributes(BNode* file, bool queryTemplate)
{
	ThrowOnError(BNodeInfo(file).SetType(
		queryTemplate ? B_QUERY_TEMPLATE_MIMETYPE : B_QUERY_MIMETYPE));

	// save date/time info for recent query support and transient query killer
	int32 currentTime = (int32)time(0);
	file->WriteAttr(kAttrQueryLastChange, B_INT32_TYPE, 0, &currentTime,
		sizeof(int32));
	int32 tmp = 1;
	file->WriteAttr("_trk/recentQuery", B_INT32_TYPE, 0, &tmp, sizeof(int32));
}


status_t
FindWindow::SaveQueryAsAttributes(BNode* file, BEntry* entry, bool queryTemplate,
	const BMessage* oldAttributes, const BPoint* oldLocation, bool temporary)
{
	if (oldAttributes != NULL) {
		// revive old window settings
		BContainerWindow::SetLayoutState(file, oldAttributes);
	}

	if (oldLocation != NULL) {
		// and the file's location
		FSSetPoseLocation(entry, *oldLocation);
	}

	BNodeInfo(file).SetType(queryTemplate
		? B_QUERY_TEMPLATE_MIMETYPE : B_QUERY_MIMETYPE);

	BString predicate;
	bool dynamicDate = false;
	GetPredicateString(predicate, dynamicDate);
	file->WriteAttrString(kAttrQueryString, &predicate);

	if (dynamicDate) {
		file->WriteAttr(kAttrDynamicDateQuery, B_BOOL_TYPE, 0, &dynamicDate,
			sizeof(dynamicDate));
	}

	int32 tmp = 1;
	file->WriteAttr("_trk/recentQuery", B_INT32_TYPE, 0, &tmp, sizeof(int32));

	fBackground->SaveDirectoryFiltersToFile(file);

	int32 firstVolumeItem, volumeItemsCount;
	BMenu* volMenu = fBackground->VolMenu(&firstVolumeItem, &volumeItemsCount);
	ASSERT(volMenu != NULL);

	int32 numberOfDirectoryFilters = fBackground->fDirectoryFilters.CountItems();
	for (int32 i = 0; i < numberOfDirectoryFilters; ++i) {
		const entry_ref* ref = fBackground->fDirectoryFilters.ItemAt(i);
		for (int32 j = 0; j < volumeItemsCount; j++) {
			BMenuItem* item = volMenu->ItemAt(firstVolumeItem + j);
			if (item->IsMarked())
				continue;
			BMessage* message = item->Message();
			dev_t device;
			if (message->FindInt32("device", &device) != B_OK)
				continue;
			if (device == ref->device)
				item->SetMarked(true);
		}
	}

	bool addAllVolumes = volMenu->ItemAt(0)->IsMarked();
	BMessage messageContainingVolumeInfo;
	for (int32 i = 0; i < volumeItemsCount; i++) {
		BMenuItem* volumeMenuItem = volMenu->ItemAt(firstVolumeItem + i);
		BMessage* messageOfVolumeMenuItem = volumeMenuItem->Message();
		dev_t device;
		if (messageOfVolumeMenuItem->FindInt32("device", &device) != B_OK)
			continue;

		if (volumeMenuItem->IsMarked() || addAllVolumes) {
			BVolume volume(device);
			EmbedUniqueVolumeInfo(&messageContainingVolumeInfo, &volume);
		}
	}

	ssize_t flattenedSize = messageContainingVolumeInfo.FlattenedSize();
	if (flattenedSize > 0) {
		BString bufferString;
		char* buffer = bufferString.LockBuffer(flattenedSize);
		messageContainingVolumeInfo.Flatten(buffer, flattenedSize);
		if (fFile->WriteAttr(kAttrQueryVolume, B_MESSAGE_TYPE, 0, buffer,
				static_cast<size_t>(flattenedSize))
			!= flattenedSize) {
			return B_ERROR;
		}
	}

	MoreOptionsStruct saveMoreOptions;
	saveMoreOptions.searchTrash = fSearchInTrash->IsMarked();
	saveMoreOptions.temporary = temporary;

	if (file->WriteAttr(kAttrQueryMoreOptions, B_RAW_TYPE, 0, &saveMoreOptions,
			sizeof(saveMoreOptions))
		== sizeof(saveMoreOptions)) {
		file->RemoveAttr(kAttrQueryMoreOptionsForeign);
	}

	fBackground->SaveWindowState(file, fEditTemplateOnly);
		// write out all the dialog items as attributes so that the query can
		// be reopened and edited later

	BView* focusedItem = CurrentFocus();
	if (focusedItem != NULL) {
		// text controls never get the focus, their internal text views do
		BView* parent = focusedItem->Parent();
		if (dynamic_cast<BTextControl*>(parent) != NULL)
			focusedItem = parent;

		// write out the current focus and, if text control, selection
		BString name(focusedItem->Name());
		file->WriteAttrString("_trk/focusedView", &name);
		BTextControl* textControl = dynamic_cast<BTextControl*>(focusedItem);
		if (textControl != NULL && textControl->TextView() != NULL) {
			int32 selStart;
			int32 selEnd;
			textControl->TextView()->GetSelection(&selStart, &selEnd);
			file->WriteAttr("_trk/focusedSelStart", B_INT32_TYPE, 0,
				&selStart, sizeof(selStart));
			file->WriteAttr("_trk/focusedSelEnd", B_INT32_TYPE, 0,
				&selEnd, sizeof(selEnd));
		}
	}

	return B_OK;
}


void
FindWindow::Save()
{
	FindSaveCommon(false);

	// close the find panel
	PostMessage(B_QUIT_REQUESTED);
}


void
FindWindow::Find()
{
	if (!FindSaveCommon(true)) {
		// have to wait for the node monitor to force old query to close
		// to avoid a race condition
		TTracker* tracker = dynamic_cast<TTracker*>(be_app);
		ASSERT(tracker != NULL);

		for (int32 timeOut = 0; ; timeOut++) {
			if (tracker != NULL && !tracker->EntryHasWindowOpen(&fRef)) {
				// window quit, we can post refs received to open a
				// new copy
				break;
			}

			// PRINT(("waiting for query window to quit, %d\n", timeOut));
			if (timeOut == 5000) {
				// the old query window would not quit for some reason
				TRESPASS();
				PostMessage(B_QUIT_REQUESTED);
				return;
			}
			snooze(1000);
		}
	}

	// tell the tracker about it
	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &fRef);
	be_app->PostMessage(&message);

	// close the find panel
	PostMessage(B_QUIT_REQUESTED);
}


bool
FindWindow::FindSaveCommon(bool find)
{
	// figure out what we need to do
	bool readFromOldFile = fFile != NULL;
	bool replaceOriginal = fFile && (!fFromTemplate || fEditTemplateOnly);
	bool keepPoseLocation = replaceOriginal;
	bool newFile = !fFile || (fFromTemplate && !fEditTemplateOnly);

	BEntry entry;
	BMessage oldAttributes;
	BPoint location;
	bool hadLocation = false;
	const char* userSpecifiedName = fBackground->UserSpecifiedName();

	if (readFromOldFile) {
		entry.SetTo(&fRef);
		BContainerWindow::GetLayoutState(fFile, &oldAttributes);
		hadLocation = FSGetPoseLocation(fFile, &location);
	}

	if (replaceOriginal) {
		fFile->Unset();
		entry.Remove();
			// remove the current entry - need to do this to quit the
			// running query and to close the corresponding window

		if (userSpecifiedName != NULL && !fEditTemplateOnly) {
			// change the name of the old query per users request
			fRef.set_name(userSpecifiedName);
			entry.SetTo(&fRef);
		}
	}

	if (newFile) {
		// create query file in the user's directory
		BPath path = GetQueriesDirectory();
		// there might be no queries folder yet, create one
		if (path.Path()[0] != '\0') {
			// either use the user specified name, or go with the name
			// generated from the predicate, etc.
			BString name;
			if (userSpecifiedName == NULL)
				GetDefaultName(name);
			else
				name << userSpecifiedName;

			if (path.Append(name.String()) == B_OK) {
				entry.SetTo(path.Path());
				entry.Remove();
				entry.GetRef(&fRef);
			}
		}
	}

	fFile = new BFile(&entry, O_RDWR | O_CREAT);
	ASSERT(fFile->InitCheck() == B_OK);

	int32 currentTime = (int32)time(0);
	fFile->WriteAttr(kAttrQueryLastChange, B_INT32_TYPE, 0, &currentTime,
		sizeof(int32));

	SaveQueryAsAttributes(fFile, &entry, !find, newFile ? 0 : &oldAttributes,
		(hadLocation && keepPoseLocation) ? &location : 0, newFile);

	return newFile;
}


void
FindWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kOpenDir:
		{
			BMessage message(B_REFS_RECEIVED);
			BEntry entry(GetQueriesDirectory().Path());
			entry_ref ref;
			if (entry.GetRef(&ref) == B_OK) {
				message.AddRef("refs", &ref);
				be_app->PostMessage(&message);
			}
			break;
		}

		case kFindButton:
			Find();
			break;

		case kSaveButton:
			Save();
			break;

		case kSaveQueryOrTemplate:
		{
			BEntry entry(&fRef);
			SaveQueryAsAttributes(fFile, &entry, IsQueryTemplate(fFile), 0, 0, false);
			break;
		}

		case kOpenSaveAsPanel:
		{
			if (fSaveAsPanel == NULL)
				fSaveAsPanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(fBackground));

			bool isTemplate;
			if (message->FindBool("saveastemplate", &isTemplate) != B_OK)
				isTemplate = false;

			BMessage* saveMessage = new BMessage(B_SAVE_REQUESTED);
			saveMessage->AddBool("includeintemplates", isTemplate);
			fSaveAsPanel->SetMessage(saveMessage);
			fSaveAsPanel->Window()->SetTitle(isTemplate ? B_TRANSLATE("Save query template:") :
				B_TRANSLATE("Save query:"));
			fSaveAsPanel->Show();
			break;
		}

		case kOpenLoadQueryPanel:
		{
			if (fOpenQueryPanel == NULL)
				fOpenQueryPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(fBackground));

			fOpenQueryPanel->SetMessage(new BMessage(kSwitchToQueryTemplate));
			fOpenQueryPanel->Window()->SetTitle(B_TRANSLATE("Open query:"));
			fOpenQueryPanel->Show();
		}

		case kSearchInTrashOptionClicked:
		{
			fSearchInTrash->SetMarked(!fSearchInTrash->IsMarked());
			break;
		}

		case kAttachFile:
		{
			entry_ref dir;
			const char* name;
			bool queryTemplate;
			if (message->FindString("name", &name) == B_OK
				&& message->FindRef("directory", &dir) == B_OK
				&& message->FindBool("template", &queryTemplate) == B_OK) {
				delete fFile;
				fFile = NULL;
				BDirectory directory(&dir);
				BEntry entry(&directory, name);
				entry_ref tmpRef;
				entry.GetRef(&tmpRef);
				fFile = TryOpening(&tmpRef);
				if (fFile != NULL) {
					fRef = tmpRef;
					fFromTemplate = IsQueryTemplate(fFile);
					SaveQueryAsAttributes(fFile, &entry, queryTemplate, 0, 0, false);
						// try to save whatever state we aleady have
						// to the new query so that if the user
						// opens it before runing it from the find panel,
						// something reasonable happens
				}
			}

			PopulateTemplatesMenu();
			fSaveQueryOrTemplateItem->SetEnabled(true);
			break;
		}

		case kSwitchToQueryTemplate:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				SwitchToTemplate(&ref);

			UpdateFileReferences(&ref);
			fBackground->LoadDirectoryFiltersFromFile(fFile);
			fSaveQueryOrTemplateItem->SetEnabled(true);
			break;
		}

		case kRunSaveAsTemplatePanel:
		{
			if (fSaveAsPanel != NULL) {
				fSaveAsPanel->Show();
			} else {
				BMessenger panel(BackgroundView());
				fSaveAsPanel = new BFilePanel(B_SAVE_PANEL, &panel);
				fSaveAsPanel->SetSaveText(B_TRANSLATE("Query template"));
				fSaveAsPanel->Window()->SetTitle(B_TRANSLATE("Save as query template:"));
				fSaveAsPanel->Show();
			}
			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


bool
FolderFilter::Filter(const entry_ref* ref, BNode* node, struct stat_beos* stat,
	const char* mimeType)
{
	ASSERT(node->InitCheck() == B_OK);
	if (node->IsDirectory()) {
		return true;
	} else if (node->IsSymLink()) {
		BEntry entry(ref, true);
		return entry.IsDirectory();
	}
	return false;
}


//	#pragma mark - FindPanel


FindPanel::FindPanel(BFile* node, FindWindow* parent, bool fromTemplate, bool editTemplateOnly)
	:
	BView("MainView", B_WILL_DRAW),
	fMode(kByNameItem),
	fAttrGrid(NULL),
	fMimeTypeMenu(NULL),
	fMimeTypeField(NULL),
	fSearchModeMenu(NULL),
	fSearchModeField(NULL),
	fVolMenu(NULL),
	fVolumeField(NULL),
	fRecentQueries(NULL),
	fMoreOptions(NULL),
	fQueryName(NULL),
	fDraggableIcon(NULL),
	fDirectorySelectPanel(NULL),
	fAddSeparatorItemState(true)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(ViewUIColor());

	uint32 initialMode = InitialMode(node);

	// add popup for mime types
	fMimeTypeMenu = new BPopUpMenu("MimeTypeMenu");
	fMimeTypeMenu->SetRadioMode(false);
	AddMimeTypesToMenu();

	fMimeTypeField = new BMenuField("MimeTypeMenu", "", fMimeTypeMenu);
	fMimeTypeField->SetDivider(0.0f);
	fMimeTypeField->MenuItem()->SetLabel(B_TRANSLATE("All files and folders"));
	// add popup for search criteria
	fSearchModeMenu = new BPopUpMenu("searchMode");
	fSearchModeMenu->AddItem(new BMenuItem(B_TRANSLATE("by name"),
		new BMessage(kByNameItem)));
	fSearchModeMenu->AddItem(new BMenuItem(B_TRANSLATE("by attribute"),
		new BMessage(kByAttributeItem)));
	fSearchModeMenu->AddItem(new BMenuItem(B_TRANSLATE("by formula"),
		new BMessage(kByFormulaItem)));

	fSearchModeMenu->ItemAt(initialMode == kByNameItem ? 0 :
		(initialMode == kByAttributeItem ? 1 : 2))->SetMarked(true);
		// mark the appropriate mode
	fSearchModeField = new BMenuField("", "", fSearchModeMenu);
	fSearchModeField->SetDivider(0.0f);

	// add popup for volume list
	fVolMenu = new BPopUpMenu("", false, false);
	fVolumeField = new BMenuField("",
		B_TRANSLATE_COMMENT("Target:",
			"The disks/folders that are searched. Similar to TextSearch's 'Set target'."),
		fVolMenu);
	fVolumeField->SetDivider(fVolumeField->StringWidth(fVolumeField->Label()) + 8);
	AddVolumes();
	fVolMenu->AddSeparatorItem();
	if (fDirectoryFilters.CountItems() > 0)
		fVolMenu->AddSeparatorItem();
	fVolMenu->AddItem(new BMenuItem(B_TRANSLATE("Select folders" B_UTF8_ELLIPSIS),
		new BMessage(kSelectDirectoryFilter)));
	LoadDirectoryFiltersFromFile(node);

	if (!editTemplateOnly) {
		BPoint draggableIconOrigin(0, 0);
		BMessage dragNDropMessage(B_SIMPLE_DATA);
		dragNDropMessage.AddInt32("be:actions", B_COPY_TARGET);
		dragNDropMessage.AddString("be:types", B_FILE_MIME_TYPE);
		dragNDropMessage.AddString("be:filetypes", kDragNDropTypes[0]);
		dragNDropMessage.AddString("be:filetypes", kDragNDropTypes[1]);
		dragNDropMessage.AddString("be:actionspecifier",
			B_TRANSLATE_NOCOLLECT(kDragNDropActionSpecifiers[0]));
		dragNDropMessage.AddString("be:actionspecifier",
			B_TRANSLATE_NOCOLLECT(kDragNDropActionSpecifiers[1]));

		BMessenger self(this);
		BRect draggableRect = DraggableIcon::PreferredRect(draggableIconOrigin,
			B_LARGE_ICON);
		fDraggableIcon = new DraggableQueryIcon(draggableRect,
			"saveHere", &dragNDropMessage, self,
			B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
		fDraggableIcon->SetExplicitMaxSize(
			BSize(draggableRect.right - draggableRect.left,
				draggableRect.bottom - draggableRect.top));
		BCursor grabCursor(B_CURSOR_ID_GRAB);
		fDraggableIcon->SetViewCursor(&grabCursor);
	}

	// add Search button
	BButton* button;
	if (editTemplateOnly) {
		button = new BButton("save", B_TRANSLATE("Save"),
			new BMessage(kSaveButton), B_FOLLOW_RIGHT + B_FOLLOW_BOTTOM);
	} else {
		button = new BButton("find", B_TRANSLATE("Search"),
			new BMessage(kFindButton), B_FOLLOW_RIGHT + B_FOLLOW_BOTTOM);
	}
	button->MakeDefault(true);

	BView* icon = fDraggableIcon;
	if (icon == NULL) {
		icon = new BBox("no draggable icon", B_WILL_DRAW, B_NO_BORDER);
		icon->SetExplicitMaxSize(BSize(0, 0));
	}

	BView* mimeTypeFieldSpacer = new BBox("MimeTypeMenuSpacer", B_WILL_DRAW, B_NO_BORDER);
	mimeTypeFieldSpacer->SetExplicitMaxSize(BSize(0, 0));

	BBox* queryControls = new BBox("Box");
	queryControls->SetBorder(B_NO_BORDER);

	BBox* queryBox = new BBox("Outer Controls");

	BGroupView* queryBoxView = new BGroupView(B_VERTICAL,
		B_USE_DEFAULT_SPACING);
	queryBoxView->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING);
	queryBox->AddChild(queryBoxView);

	button->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT, B_ALIGN_BOTTOM));

	BLayoutBuilder::Group<>(queryBoxView, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fMimeTypeField)
			.Add(mimeTypeFieldSpacer)
			.Add(fSearchModeField)
			.AddStrut(B_USE_DEFAULT_SPACING)
			.Add(fVolumeField)
			.End()
		.Add(new BSeparatorView(B_HORIZONTAL, B_PLAIN_BORDER))
		.Add(queryControls);
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(queryBox)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGroup(B_VERTICAL)
				.AddGlue()
				.Add(icon)
				.AddGlue()
			.End()
			.AddGlue()
			.AddGroup(B_VERTICAL)
				.Add(button)
			.End();

	if (initialMode != kByAttributeItem)
		AddByNameOrFormulaItems();
	else
		AddByAttributeItems(node);
}


FindPanel::~FindPanel()
{
	int32 count = fDirectoryFilters.CountItems();
	for (int32 i = 0; i < count; i++)
		delete fDirectoryFilters.RemoveItemAt(i);
}


status_t
FindPanel::AddDirectoryFiltersToMenu(BMenu* menu, BHandler* target)
{
	if (menu == NULL)
		return B_BAD_VALUE;

	int32 count = fDirectoryFilters.CountItems();
	for (int32 i = 0; i < count; i++) {
		entry_ref* filter = fDirectoryFilters.ItemAt(i);
		if (filter != NULL)
			FindPanel::AddDirectoryFilterItemToMenu(menu, filter, target);
	}

	return B_OK;
}


void
FindPanel::LoadDirectoryFiltersFromFile(const BNode* node)
{
	if (node == NULL)
		return;

	struct attr_info info;
	if (node->GetAttrInfo("_trk/directories", &info) != B_OK)
		return;

	BString bufferString;
	char* buffer = bufferString.LockBuffer(info.size);
	if (node->ReadAttr("_trk/directories", B_MESSAGE_TYPE, 0, buffer, (size_t)info.size)
		!= info.size) {
		return;
	}

	BMessage message;
	if (message.Unflatten(buffer) != B_OK)
		return;

	int32 count;
	if (message.GetInfo("refs", NULL, &count) != B_OK)
		return;

	for (int32 i = 0; i < count; i++) {
		entry_ref ref;
		if (message.FindRef("refs", i, &ref) != B_OK)
			continue;

		BEntry entry(&ref);
		if (entry.InitCheck() == B_OK && entry.Exists() && !entry.IsDirectory())
			continue;

		AddDirectoryFilter(&ref);
	}

	bufferString.UnlockBuffer();
}


status_t
FindPanel::AddDirectoryFilterItemToMenu(BMenu* menu, const entry_ref* ref, BHandler* target,
	int32 index)
{
	if (menu == NULL || ref == NULL || target == NULL)
		return B_BAD_VALUE;

	BEntry entry(ref, true);
	if (entry.InitCheck() != B_OK)
		return B_ERROR;

	if (entry.Exists() && entry.IsDirectory()) {
		entry_ref symlinkTraversedDirectory;
		entry.GetRef(&symlinkTraversedDirectory);

		// Adding the options into the fVolMenu
		Model model(&entry);
		BMenuItem* item = new ModelMenuItem(&model, model.Name(), NULL);
		BMessage* message = new BMessage(kRemoveDirectoryFilter);
		message->AddPointer("pointer", item);
		message->AddRef("refs", &symlinkTraversedDirectory);
		item->SetMessage(message);
		item->SetMarked(true);
		item->SetTarget(target);

		bool status = false;
		if (index == -1)
			status = menu->AddItem(item);
		else
			status = menu->AddItem(item, index);

		return status ? B_OK : B_ERROR;

	} else if (!entry.IsDirectory()) {
		return B_NOT_A_DIRECTORY;
	} else {
		return B_ENTRY_NOT_FOUND;
	}
}


status_t
FindPanel::AddDirectoryFilter(const entry_ref* ref, bool addToMenu)
{
	if (ref == NULL)
		return B_BAD_VALUE;

	// Check for Duplicate Entry
	int32 count = fDirectoryFilters.CountItems();
	for (int32 i = 0; i < count; i++) {
		entry_ref* item = fDirectoryFilters.ItemAt(i);
		if (ref != NULL && item != NULL && *item == *ref)
			return B_CANCELED;
	}

	status_t error = B_OK;

	if (addToMenu) {
		if (fAddSeparatorItemState) {
			BMenuItem* addDirectoriesItem = fVolMenu->RemoveItem(fVolMenu->CountItems() - 1);
			error = FindPanel::AddDirectoryFilterItemToMenu(fVolMenu, ref, this);
			fVolMenu->AddSeparatorItem();
			fVolMenu->AddItem(addDirectoriesItem);
			fAddSeparatorItemState = false;
		} else {
			int32 index = fVolMenu->CountItems() - 2;
			error = FindPanel::AddDirectoryFilterItemToMenu(fVolMenu, ref, this, index);
		}

		UnmarkDisks();
	}

	if (error == B_OK) {
		fDirectoryFilters.AddItem(new entry_ref(*ref));
		return B_OK;
	} else {
		return B_ERROR;
	}
}


void
FindPanel::RemoveDirectoryFilter(const entry_ref* ref)
{
	ASSERT(ref != NULL);
	int32 count = fDirectoryFilters.CountItems();
	for (int32 i = 0; i < count; i++) {
		entry_ref* item = fDirectoryFilters.ItemAt(i);
		if (item != NULL && ref != NULL && (*item) == (*ref))
			fDirectoryFilters.RemoveItemAt(i);
	}
}


status_t
FindPanel::SaveDirectoryFiltersToFile(BNode* node)
{
	if (node->InitCheck() != B_OK)
		return B_NO_INIT;

	int32 count = fDirectoryFilters.CountItems();
	if (count == 0) {
		node->RemoveAttr("_trk/directories");
		return B_OK;
	}

	// Store the entry_refs of the fDirectoryFilters to a BMessage
	// So that it can be serialized.
	BMessage message;
	for (int32 i = 0; i < count; i++) {
		entry_ref* ref = fDirectoryFilters.ItemAt(i);
		if (message.AddRef("refs", ref) != B_OK)
			return B_ERROR;
	}

	// Serialize and Write the Attribute
	ssize_t size = message.FlattenedSize();
	BString bufferString;
	char* buffer = bufferString.LockBuffer(size);
	if (message.Flatten(buffer, size) == B_OK) {
		if (node->WriteAttr("_trk/directories", B_MESSAGE_TYPE, 0, buffer, (size_t)size) != size)
			return B_IO_ERROR;
		else
			return B_OK;
	}

	return B_ERROR;
}


void
FindPanel::AttachedToWindow()
{
	_inherited::AttachedToWindow();

	FindWindow* findWindow = dynamic_cast<FindWindow*>(Window());
	ASSERT(findWindow != NULL);

	if (findWindow == NULL)
		return;

	BNode* node = findWindow->QueryNode();
	fSearchModeMenu->SetTargetForItems(this);
	RestoreMimeTypeMenuSelection(node);
		// preselect the mime we used the last time have to do it here
		// because AddByAttributeItems will build different menus based
		// on which mime type is preselected
	RestoreWindowState(node);

	if (!findWindow->CurrentFocus()) {
		// try to pick a good focus if we restore to one already
		BTextControl* textControl
			= dynamic_cast<BTextControl*>(FindView("TextControl"));
		if (textControl == NULL) {
			// pick the last text control in the attribute view
			BString title("TextEntry");
			title << (fAttrGrid->CountRows() - 1);
			textControl = dynamic_cast<BTextControl*>(FindView(title.String()));
		}
		if (textControl != NULL)
			textControl->MakeFocus();
	}

	BButton* button = dynamic_cast<BButton*>(FindView("remove button"));
	if (button != NULL)
		button->SetTarget(this);

	button = dynamic_cast<BButton*>(FindView("add button"));
	if (button != NULL)
		button->SetTarget(this);

	fVolMenu->SetTargetForItems(this);

	// set target for MIME type items
	for (int32 index = MimeTypeMenu()->CountItems(); index-- > 2;) {
		BMenu* submenu = MimeTypeMenu()->ItemAt(index)->Submenu();
		if (submenu != NULL)
			submenu->SetTargetForItems(this);
	}
	fMimeTypeMenu->SetTargetForItems(this);

	// set the MIME type to the default value, if no value is already selected
	if (fMimeTypeMenu->FindMarked() == NULL) {
		BMenuItem* firstItem = fMimeTypeMenu->ItemAt(0);
		if (firstItem != NULL)
			firstItem->SetMarked(true);
	}

	// resize menu fields after marking them
	ResizeMenuField(fMimeTypeField);
	ResizeMenuField(fSearchModeField);
	ResizeMenuField(fVolumeField);

	if (fDraggableIcon != NULL)
		fDraggableIcon->SetTarget(BMessenger(this));
}


void
FindPanel::ResizeMenuField(BMenuField* menuField)
{
	ASSERT(menuField != NULL);
	if (menuField == NULL)
		return;

	BMenuBar* menuBar = menuField->MenuBar();
	ASSERT(menuBar != NULL);
	if (menuBar == NULL)
		return;

	BSize size;
	menuBar->GetPreferredSize(&size.width, &size.height);

	BMenu* menu = menuField->Menu();
	ASSERT(menu != NULL);
	if (menu == NULL)
		return;

	float padding = 0.0f;
	float width = 0.0f;

	BMenuItem* markedItem = menu->FindMarked();
	if (markedItem != NULL) {
		if (markedItem->Submenu() != NULL) {
			BMenuItem* subItem = markedItem->Submenu()->FindMarked();
			if (subItem != NULL && subItem->Label() != NULL) {
				float labelWidth = menuField->StringWidth(subItem->Label());
				padding = size.width - labelWidth;
			}
		} else if (markedItem->Label() != NULL) {
			float labelWidth = menuField->StringWidth(markedItem->Label());
			padding = size.width - labelWidth;
		}
	}

	for (int32 index = menu->CountItems(); index-- > 0; ) {
		BMenuItem* item = menu->ItemAt(index);
		if (item == NULL)
			continue;

		BMenu* submenu = item->Submenu();
		if (submenu != NULL) {
			for (int32 subIndex = submenu->CountItems(); subIndex-- > 0; ) {
				BMenuItem* subItem = submenu->ItemAt(subIndex);
				if (subItem->Label() == NULL)
					continue;

				width = std::max(width, menuField->StringWidth(subItem->Label()));
			}
		} else if (item->Label() != NULL) {
			width = std::max(width, menuField->StringWidth(item->Label()));
		}
	}

	// clip to reasonable min and max width
	float minW = 0;
	if (menuField == fVolumeField)
		minW = menuField->StringWidth(MultipleSelectionsTitle(99));
	else
		minW = be_control_look->DefaultLabelSpacing() * 10;
	float maxW = be_control_look->DefaultLabelSpacing() * 30;
	width = std::max(width, minW);
	width = std::min(width, maxW);

	size.width = width + padding;

	// set max content width to truncate long name
	menuBar->SetMaxContentWidth(size.width);

	// add room for pop-up indicator
	size.width += kPopUpIndicatorWidth;

	// make first-level menu width match
	menu->SetMaxContentWidth(size.width);

	menuBar->SetExplicitSize(size);
}


static void
PopUpMenuSetTitle(BMenu* menu, const char* title)
{
	// This should really be in BMenuField
	BMenu* bar = menu->Supermenu();

	ASSERT(bar);
	ASSERT(bar->ItemAt(0));
	if (bar == NULL || !bar->ItemAt(0))
		return;

	bar->ItemAt(0)->SetLabel(title);
}


void
FindPanel::ShowVolumeMenuLabel()
{
	// find out if more than one items are marked
	int32 selectedVolumesCount = 0;

	BMenuItem* lastSelectedVolumeItem = NULL;
	for (int32 i = 0; i < fVolumeItemsCount; ++i) {
		BMenuItem* volumeItem = fVolMenu->ItemAt(fFirstVolumeItem + i);
		if (volumeItem->IsMarked()) {
			selectedVolumesCount++;
			lastSelectedVolumeItem = volumeItem;
		}
	}

	bool allVolumes = selectedVolumesCount == fVolumeItemsCount;

	if (selectedVolumesCount == 0 && fDirectoryFilters.CountItems() == 1) {
		// 1 directory filter selected
		fVolMenu->ItemAt(0)->SetMarked(false);
		PopUpMenuSetTitle(fVolMenu, fDirectoryFilters.ItemAt(0)->name);
	} else if (selectedVolumesCount == 1 && fDirectoryFilters.CountItems() == 0) {
		// 1 volume selected
		fVolMenu->ItemAt(0)->SetMarked(false);
		PopUpMenuSetTitle(fVolMenu, lastSelectedVolumeItem->Label());
	} else if (fDirectoryFilters.CountItems() > 1 || (!allVolumes && selectedVolumesCount > 1)) {
		// multiple selections
		fVolMenu->ItemAt(0)->SetMarked(false);
		int32 selectedCount = selectedVolumesCount + fDirectoryFilters.CountItems();
		PopUpMenuSetTitle(fVolMenu, MultipleSelectionsTitle(selectedCount));
	} else {
		// All disks (no selection or all volumes selected)
		fVolMenu->ItemAt(0)->SetMarked(true);
		PopUpMenuSetTitle(fVolMenu, fVolMenu->ItemAt(0)->Label());
	}
}


BString
FindPanel::MultipleSelectionsTitle(int32 count)
{
	static BStringFormat format(B_TRANSLATE_COMMENT(
		"{0, plural, one{# selected} other{# selected}}",
		"\"1 selected (singular)\" or \"2 selected (plural)\""));
	BString selected;
	format.Format(selected, count);

	return selected;
}


void
FindPanel::Draw(BRect)
{
	if (fAttrGrid == NULL)
		return;

	for (int32 index = 0; index < fAttrGrid->CountRows(); index++) {
		BMenuField* menuField = dynamic_cast<BMenuField*>(FindAttrView("MenuField", index));
		if (menuField == NULL)
			continue;

		BLayoutItem* stringViewLayoutItem = fAttrGrid->ItemAt(1, index);
		if (stringViewLayoutItem == NULL)
			continue;

		BMenu* menuFieldMenu = menuField->Menu();
		if (menuFieldMenu == NULL)
			continue;

		BMenuItem* item = menuFieldMenu->FindMarked();
		if (item == NULL || item->Submenu() == NULL
			|| item->Submenu()->FindMarked() == NULL) {
			continue;
		}

		if (stringViewLayoutItem == NULL) {
			stringViewLayoutItem = fAttrGrid->AddView(new BStringView("",
				item->Submenu()->FindMarked()->Label()), 1, index);
			stringViewLayoutItem->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
				B_ALIGN_VERTICAL_UNSET));
		}

		if (stringViewLayoutItem != NULL) {
			BStringView* stringView
				= dynamic_cast<BStringView*>(stringViewLayoutItem->View());
			if (stringView != NULL) {
				BMenu* submenu = item->Submenu();
				if (submenu != NULL) {
					BMenuItem* selected = submenu->FindMarked();
					if (selected != NULL)
						stringView->SetText(selected->Label());
				}
			}
		}
	}
}


void
FindPanel::UnmarkDisks()
{
	for (int32 i = 0; i < fVolumeItemsCount; ++i)
		fVolMenu->ItemAt(fFirstVolumeItem + i)->SetMarked(false);

	fVolMenu->ItemAt(0)->SetMarked(false);
}


void
FindPanel::MessageReceived(BMessage* message)
{
	entry_ref dir;
	const char* name;
	BMenuItem* item;

	switch (message->what) {
		case kVolumeItem:
		{
			// volume changed
			BMenuItem* invokedItem;
			dev_t dev;
			if (message->FindPointer("source", (void**)&invokedItem) != B_OK)
				return;

			if (message->FindInt32("device", &dev) != B_OK)
				break;

			BMenu* menu = invokedItem->Menu();
			ASSERT(menu);

			if (dev == -1) {
				// all disks selected, uncheck everything else
				int32 count = 0;
				BVolumeRoster roster;
				BVolume volume;
				while (roster.GetNextVolume(&volume) == B_OK) {
					if (volume.IsPersistent() && volume.KnowsQuery()) {
						BDirectory root;
						if (volume.GetRootDirectory(&root) != B_OK)
							continue;
						count++;
					}
				}
				for (int32 index = 2; index < count + 2; index++)
					menu->ItemAt(index)->SetMarked(false);

				// make all disks the title and check it
				PopUpMenuSetTitle(menu, menu->ItemAt(0)->Label());
				menu->ItemAt(0)->SetMarked(true);
			} else {
				// a specific volume selected, unmark "all disks"
				menu->ItemAt(0)->SetMarked(false);

				// toggle mark on invoked item
				int32 count = menu->CountItems();
				for (int32 index = 2; index < count; index++) {
					BMenuItem* item = menu->ItemAt(index);

					if (invokedItem == item) {
						// we just selected this
						bool wasMarked = item->IsMarked();
						item->SetMarked(!wasMarked);
					}
				}
			}

			int32 count = fVolMenu->CountItems();
			int32 startingIndex = 3 + fVolumeItemsCount;
			int32 endingIndex = count - 2;
			for (int32 i = startingIndex; i < endingIndex; ++i) {
				BMenuItem* menuItem = fVolMenu->ItemAt(i);
				BMessage* message = menuItem->Message();
				entry_ref ref;
				if (!message || message->FindRef("refs", &ref) != B_OK)
					continue;

				RemoveDirectoryFilter(&ref);
				menuItem->SetMarked(false);
			}

			// make sure the right label is showing
			ShowVolumeMenuLabel();

			break;
		}

		case kSelectDirectoryFilter:
		{
			if (fDirectorySelectPanel == NULL) {
				BRefFilter* filter = new FolderFilter();
				fDirectorySelectPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
					B_DIRECTORY_NODE, true, new BMessage(kAddDirectoryFilters), filter);
			}

			fDirectorySelectPanel->Window()->SetTitle(B_TRANSLATE("Select folders"));
			fDirectorySelectPanel->Show();
			break;
		}

		case kAddDirectoryFilters:
		{
			int32 count;
			message->GetInfo("refs", NULL, &count);
			for (int32 i = 0; i < count; i++) {
				entry_ref ref;
				status_t error = message->FindRef("refs", i, &ref);
				if (error == B_OK)
					AddDirectoryFilter(&ref);
			}
			ShowVolumeMenuLabel();
			break;
		}

		case kRemoveDirectoryFilter:
		{
			BMenuItem* item;
			entry_ref ref;
			if (message->FindPointer("pointer", (void**)&item) == B_OK
				&& message->FindRef("refs", &ref) == B_OK) {

				if (item->IsMarked()) {
					RemoveDirectoryFilter(&ref);
					item->SetMarked(false);
				} else {
					AddDirectoryFilter(&ref, false);
					item->SetMarked(true);
					UnmarkDisks();
				}

				ShowVolumeMenuLabel();
			}
			break;
		}

		case kByAttributeItem:
		case kByNameItem:
		case kByFormulaItem:
			SwitchMode(message->what);
			break;

		case kAddItem:
			AddAttrRow();
			break;

		case kRemoveItem:
			RemoveAttrRow();
			break;

		case kMIMETypeItem:
		{
			BMenuItem* item;
			if (message->FindPointer("source", (void**)&item) == B_OK) {
				// don't add the "All files and folders" to the list
				if (fMimeTypeMenu->IndexOf(item) != 0)
					gMostUsedMimeTypes.AddName(item->Label());

				SetCurrentMimeType(item);
			}
			if (fMode == kByAttributeItem) {
				// the attributes for this type may be different
				RemoveAttrViewItems(false);
				AddAttrRow();
			}

			break;
		}

		case kAttributeItem:
			if (message->FindPointer("source", (void**)&item) != B_OK)
				return;

			item->Menu()->Superitem()->SetMarked(true);
			Invalidate();
			break;

		case kAttributeItemMain:
			// in case someone selected just an attribute without the
			// comparator
			if (message->FindPointer("source", (void**)&item) != B_OK)
				return;

			if (item->Submenu()->ItemAt(0) != NULL)
				item->Submenu()->ItemAt(0)->SetMarked(true);

			Invalidate();
			break;

		case B_SAVE_REQUESTED:
		{
			// finish saving query template from a SaveAs panel
			entry_ref ref;
			status_t error = message->FindRef("refs", &ref);

			if (error == B_OK) {
				// direct entry selected, convert to parent dir and name
				BEntry entry(&ref);
				error = entry.GetParent(&entry);
				if (error == B_OK) {
					entry.GetRef(&dir);
					name = ref.name;
				}
			} else {
				// parent dir and name selected
				error = message->FindRef("directory", &dir);
				if (error == B_OK)
					error = message->FindString("name", &name);
			}

			bool includeInTemplates;

			if (error == B_OK
				&& message->FindBool("includeintemplates", &includeInTemplates) == B_OK) {
				SaveAsQueryOrTemplate(&dir, name, includeInTemplates);
			}
			break;
		}

		case B_COPY_TARGET:
		{
			// finish drag&drop
			const char* str;
			const char* mimeType = NULL;
			const char* actionSpecifier = NULL;

			if (message->FindString("be:types", &str) == B_OK
				&& strcasecmp(str, B_FILE_MIME_TYPE) == 0
				&& (message->FindString("be:actionspecifier",
						&actionSpecifier) == B_OK
					|| message->FindString("be:filetypes", &mimeType) == B_OK)
				&& message->FindString("name", &name) == B_OK
				&& message->FindRef("directory", &dir) == B_OK) {

				bool query = false;
				bool queryTemplate = false;

				if (actionSpecifier
					&& strcasecmp(actionSpecifier,
						B_TRANSLATE_NOCOLLECT(
							kDragNDropActionSpecifiers[0])) == 0) {
					query = true;
				} else if (actionSpecifier
					&& strcasecmp(actionSpecifier,
						B_TRANSLATE_NOCOLLECT(
							kDragNDropActionSpecifiers[1])) == 0) {
					queryTemplate = true;
				} else if (mimeType && strcasecmp(mimeType,
						kDragNDropTypes[0]) == 0) {
					query = true;
				} else if (mimeType && strcasecmp(mimeType,
					kDragNDropTypes[1]) == 0) {
					queryTemplate = true;
				}

				if (query || queryTemplate)
					SaveAsQueryOrTemplate(&dir, name, queryTemplate);
			}

			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


void
FindPanel::SaveAsQueryOrTemplate(const entry_ref* dir, const char* name,
	bool queryTemplate)
{
	BDirectory directory(dir);
	BFile file(&directory, name, O_RDWR | O_CREAT | O_TRUNC);
	BNodeInfo(&file).SetType(queryTemplate
		? B_QUERY_TEMPLATE_MIMETYPE : B_QUERY_MIMETYPE);

	BMessage attach(kAttachFile);
	attach.AddRef("directory", dir);
	attach.AddString("name", name);
	attach.AddBool("template", queryTemplate);
	Window()->PostMessage(&attach, 0);
}


BView*
FindPanel::FindAttrView(const char* name, int row) const
{
	for (int32 index = 0; index < fAttrGrid->CountColumns(); index++) {

		BLayoutItem* item = fAttrGrid->ItemAt(index, row);
		if (item == NULL)
			continue;

		BView* view = item->View();
		if (view == NULL)
			continue;

		view = view->FindView(name);
		if (view != NULL)
			return view;

	}

	return NULL;
}


void
FindPanel::BuildAttrQuery(BQuery* query, bool& dynamicDate) const
{
	dynamicDate = false;

	// go through each attrview and add the attr and comparison info
	for (int32 index = 0; index < fAttrGrid->CountRows(); index++) {

		BString title;
		title << "TextEntry" << index;

		BTextControl* textControl = dynamic_cast<BTextControl*>(
			FindAttrView(title, index));
		if (textControl == NULL)
			return;

		BMenuField* menuField = dynamic_cast<BMenuField*>(
			FindAttrView("MenuField", index));
		if (menuField == NULL)
			return;

		BMenuItem* item = menuField->Menu()->FindMarked();
		if (item == NULL)
			continue;

		BMessage* message = item->Message();
		int32 type;
		if (message->FindInt32("type", &type) == B_OK) {

			const char* str;
			if (message->FindString("name", &str) == B_OK)
				query->PushAttr(str);
			else
				query->PushAttr(item->Label());

			switch (type) {
				case B_STRING_TYPE:
					query->PushString(textControl->Text(), true);
					break;

				case B_TIME_TYPE:
				{
					int flags = 0;
					DEBUG_ONLY(time_t result =)
					parsedate_etc(textControl->Text(), -1,
						&flags);
					dynamicDate = (flags & PARSEDATE_RELATIVE_TIME) != 0;
					PRINT(("parsedate_etc - date is %srelative, %"
						B_PRIdTIME "\n",
						dynamicDate ? "" : "not ", result));

					query->PushDate(textControl->Text());
					break;
				}

				case B_BOOL_TYPE:
				{
					uint32 value;
					if (strcasecmp(textControl->Text(),
							"true") == 0) {
						value = 1;
					} else if (strcasecmp(textControl->Text(),
							"false") == 0) {
						value = 0;
					} else
						value = (uint32)atoi(textControl->Text());

					value %= 2;
					query->PushUInt32(value);
					break;
				}

				case B_UINT8_TYPE:
				case B_UINT16_TYPE:
				case B_UINT32_TYPE:
					query->PushUInt32((uint32)StringToScalar(
						textControl->Text()));
					break;

				case B_INT8_TYPE:
				case B_INT16_TYPE:
				case B_INT32_TYPE:
					query->PushInt32((int32)StringToScalar(
						textControl->Text()));
					break;

				case B_UINT64_TYPE:
					query->PushUInt64((uint64)StringToScalar(
						textControl->Text()));
					break;

				case B_OFF_T_TYPE:
				case B_INT64_TYPE:
					query->PushInt64(StringToScalar(
						textControl->Text()));
					break;

				case B_FLOAT_TYPE:
				{
					float floatVal;
					sscanf(textControl->Text(), "%f",
						&floatVal);
					query->PushFloat(floatVal);
					break;
				}

				case B_DOUBLE_TYPE:
				{
					double doubleVal;
					sscanf(textControl->Text(), "%lf",
						&doubleVal);
					query->PushDouble(doubleVal);
					break;
				}
			}
		}

		query_op theOperator;
		BMenuItem* operatorItem = item->Submenu()->FindMarked();
		if (operatorItem && operatorItem->Message() != NULL) {
			operatorItem->Message()->FindInt32("operator",
				(int32*)&theOperator);
			query->PushOp(theOperator);
		} else {
			query->PushOp(B_EQ);
		}

		// add logic based on selection in Logic menufield
		if (index > 0) {
			menuField = dynamic_cast<BMenuField*>(
				FindAttrView("Logic", index - 1));
			if (menuField != NULL) {
				item = menuField->Menu()->FindMarked();
				if (item != NULL) {
					message = item->Message();
					message->FindInt32("combine", (int32*)&theOperator);
					query->PushOp(theOperator);
				}
			} else {
				query->PushOp(B_AND);
			}
		}
	}
}


void
FindPanel::PushMimeType(BQuery* query) const
{
	const char* type;
	if (CurrentMimeType(&type) == NULL)
		return;

	if (strcmp(kAllMimeTypes, type)) {
		// add an asterisk if we are searching for a supertype
		char buffer[B_FILE_NAME_LENGTH];
		if (strchr(type, '/') == NULL) {
			strlcpy(buffer, type, sizeof(buffer));
			strlcat(buffer, "/*", sizeof(buffer));
			type = buffer;
		}

		query->PushAttr(kAttrMIMEType);
		query->PushString(type);
		query->PushOp(B_EQ);
		query->PushOp(B_AND);
	}
}


void
FindPanel::GetByAttrPredicate(BQuery* query, bool& dynamicDate) const
{
	ASSERT(Mode() == (int32)kByAttributeItem);
	BuildAttrQuery(query, dynamicDate);
	PushMimeType(query);
}


void
FindPanel::GetDefaultName(BString& name) const
{
	BTextControl* textControl = dynamic_cast<BTextControl*>(
		FindView("TextControl"));

	switch (Mode()) {
		case kByNameItem:
			if (textControl != NULL) {
				name.SetTo(B_TRANSLATE_COMMENT("Name = %name",
					"FindResultTitle"));
				name.ReplaceFirst("%name", textControl->Text());
			}
			break;

		case kByFormulaItem:
			if (textControl != NULL) {
				name.SetTo(B_TRANSLATE_COMMENT("Formula %formula",
					"FindResultTitle"));
				name.ReplaceFirst("%formula", textControl->Text());
			}
			break;

		case kByAttributeItem:
		{
			BMenuItem* item = fMimeTypeMenu->FindMarked();
			if (item != NULL)
				name << item->Label() << ": ";

			for (int32 i = 0; i < fAttrGrid->CountRows(); i++) {
				GetDefaultAttrName(name, i);
				if (i + 1 < fAttrGrid->CountRows())
					name << ", ";
			}
			break;
		}
	}
}


const char*
FindPanel::UserSpecifiedName() const
{
	return NULL;
}


void
FindPanel::GetByNamePredicate(BQuery* query) const
{
	ASSERT(Mode() == (int32)kByNameItem);

	BTextControl* textControl
		= dynamic_cast<BTextControl*>(FindView("TextControl"));

	ASSERT(textControl != NULL);

	if (textControl == NULL)
		return;

	query->PushAttr("name");
	query->PushString(textControl->Text(), true);

	if (strstr(textControl->Text(), "*") != NULL) {
		// assume pattern is a regular expression, try doing an exact match
		query->PushOp(B_EQ);
	} else
		query->PushOp(B_CONTAINS);

	PushMimeType(query);
}


void
FindPanel::SwitchMode(uint32 mode)
{
	if (fMode == mode)
		// no work, bail
		return;

	uint32 oldMode = fMode;
	BString buffer;

	switch (mode) {
		case kByFormulaItem:
		{
			if (oldMode == kByAttributeItem || oldMode == kByNameItem) {
				BQuery query;
				if (oldMode == kByAttributeItem) {
					bool dummy;
					GetByAttrPredicate(&query, dummy);
				} else
					GetByNamePredicate(&query);

				query.GetPredicate(&buffer);
			}
		}
		// fall-through
		case kByNameItem:
		{
			fMode = mode;
			RemoveByAttributeItems();
			ShowOrHideMimeTypeMenu();
			AddByNameOrFormulaItems();

			if (buffer.Length() > 0) {
				ASSERT(mode == kByFormulaItem
					|| oldMode == kByAttributeItem);
				BTextControl* textControl
					= dynamic_cast<BTextControl*>(FindView("TextControl"));
				if (textControl != NULL)
					textControl->SetText(buffer.String());
			}
			break;
		}

		case kByAttributeItem:
		{
			fMode = mode;
			BTextControl* textControl
				= dynamic_cast<BTextControl*>(FindView("TextControl"));
			if (textControl != NULL) {
				textControl->RemoveSelf();
				delete textControl;
			}

			ShowOrHideMimeTypeMenu();
			AddAttrRow();
			break;
		}
	}
}


BMenuItem*
FindPanel::CurrentMimeType(const char** type) const
{
	// search for marked item in the list
	BMenuItem* item = MimeTypeMenu()->FindMarked();

	if (item != NULL && MimeTypeMenu()->IndexOf(item) != 0
		&& item->Submenu() == NULL) {
		// if it's one of the most used items, ignore it
		item = NULL;
	}

	if (item == NULL) {
		for (int32 index = MimeTypeMenu()->CountItems(); index-- > 0;) {
			BMenu* submenu = MimeTypeMenu()->ItemAt(index)->Submenu();
			if (submenu != NULL && (item = submenu->FindMarked()) != NULL)
				break;
		}
	}

	if (type != NULL && item != NULL) {
		BMessage* message = item->Message();
		if (message == NULL)
			return NULL;

		if (message->FindString("mimetype", type) != B_OK)
			return NULL;
	}
	return item;
}


status_t
FindPanel::SetCurrentMimeType(BMenuItem* item)
{
	// unmark old MIME type (in most used list, and the tree)

	BMenuItem* marked = CurrentMimeType();
	if (marked != NULL) {
		marked->SetMarked(false);

		if ((marked = MimeTypeMenu()->FindMarked()) != NULL)
			marked->SetMarked(false);
	}

	// mark new MIME type (in most used list, and the tree)

	if (item != NULL) {
		item->SetMarked(true);
		fMimeTypeField->MenuItem()->SetLabel(item->Label());

		BMenuItem* search;
		for (int32 i = 2; (search = MimeTypeMenu()->ItemAt(i)) != NULL; i++) {
			if (item == search || search->Label() == NULL)
				continue;

			if (strcmp(item->Label(), search->Label()) == 0) {
				search->SetMarked(true);
				break;
			}

			BMenu* submenu = search->Submenu();
			if (submenu == NULL)
				continue;

			for (int32 j = submenu->CountItems(); j-- > 0;) {
				BMenuItem* sub = submenu->ItemAt(j);
				if (strcmp(item->Label(), sub->Label()) == 0) {
					sub->SetMarked(true);
					break;
				}
			}
		}
	}

	return B_OK;
}


status_t
FindPanel::SetCurrentMimeType(const char* label)
{
	// unmark old MIME type (in most used list, and the tree)

	BMenuItem* marked = CurrentMimeType();
	if (marked != NULL) {
		marked->SetMarked(false);

		if ((marked = MimeTypeMenu()->FindMarked()) != NULL)
			marked->SetMarked(false);
	}

	// mark new MIME type (in most used list, and the tree)

	fMimeTypeField->MenuItem()->SetLabel(label);
	bool found = false;

	for (int32 index = MimeTypeMenu()->CountItems(); index-- > 0;) {
		BMenuItem* item = MimeTypeMenu()->ItemAt(index);
		BMenu* submenu = item->Submenu();
		if (submenu != NULL && !found) {
			for (int32 subIndex = submenu->CountItems(); subIndex-- > 0;) {
				BMenuItem* subItem = submenu->ItemAt(subIndex);
				if (subItem->Label() != NULL
					&& strcmp(label, subItem->Label()) == 0) {
					subItem->SetMarked(true);
					found = true;
				}
			}
		}

		if (item->Label() != NULL && strcmp(label, item->Label()) == 0) {
			item->SetMarked(true);
			return B_OK;
		}
	}

	return found ? B_OK : B_ENTRY_NOT_FOUND;
}


static void
AddSubtype(BString& text, const BMimeType& type)
{
	text.Append(" (");
	text.Append(strchr(type.Type(), '/') + 1);
		// omit the slash
	text.Append(")");
}


bool
FindPanel::AddOneMimeTypeToMenu(const ShortMimeInfo* info, void* castToMenu)
{
	BPopUpMenu* menu = static_cast<BPopUpMenu*>(castToMenu);

	BMimeType type(info->InternalName());
	BMimeType super;
	type.GetSupertype(&super);
	if (super.InitCheck() < B_OK)
		return false;

	BMenuItem* superItem = menu->FindItem(super.Type());
	if (superItem != NULL) {
		BMessage* message = new BMessage(kMIMETypeItem);
		message->AddString("mimetype", info->InternalName());

		// check to ensure previous item's name differs
		BMenu* menu = superItem->Submenu();
		BMenuItem* previous = menu->ItemAt(menu->CountItems() - 1);
		BString text = info->ShortDescription();
		if (previous != NULL
			&& strcasecmp(previous->Label(), info->ShortDescription()) == 0) {
			AddSubtype(text, type);

			// update the previous item as well
			BMimeType type(previous->Message()->GetString("mimetype", NULL));
			BString label = ShortMimeInfo(type).ShortDescription();
			AddSubtype(label, type);
			previous->SetLabel(label.String());
		}

		menu->AddItem(new IconMenuItem(text.String(), message,
			info->InternalName()));
	}

	return false;
}


void
FindPanel::AddMimeTypesToMenu()
{
	BMessage* itemMessage = new BMessage(kMIMETypeItem);
	itemMessage->AddString("mimetype", kAllMimeTypes);

	IconMenuItem* firstItem = new IconMenuItem(
		B_TRANSLATE("All files and folders"), itemMessage,
		static_cast<BBitmap*>(NULL));
	MimeTypeMenu()->AddItem(firstItem);
	MimeTypeMenu()->AddSeparatorItem();

	// add recent MIME types

	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	ASSERT(tracker != NULL);

	BStringList list;
	if (tracker != NULL && gMostUsedMimeTypes.ObtainList(&list)) {
		int32 count = 0;
		for (int32 index = 0; index < list.CountStrings(); index++) {
			BString name = list.StringAt(index);

			MimeTypeList* mimeTypes = tracker->MimeTypes();
			if (mimeTypes != NULL) {
				const ShortMimeInfo* info = mimeTypes->FindMimeType(name);
				if (info == NULL)
					continue;

				BMessage* message = new BMessage(kMIMETypeItem);
				message->AddString("mimetype", info->InternalName());

				MimeTypeMenu()->AddItem(new BMenuItem(name, message));
				count++;
			}
		}
		if (count != 0)
			MimeTypeMenu()->AddSeparatorItem();

		gMostUsedMimeTypes.ReleaseList();
	}

	// add MIME type tree list

	BMessage types;
	if (BMimeType::GetInstalledSupertypes(&types) == B_OK) {
		const char* superType;
		int32 index = 0;

		while (types.FindString("super_types", index++, &superType) == B_OK) {
			BMenu* superMenu = new BMenu(superType);

			BMessage* message = new BMessage(kMIMETypeItem);
			message->AddString("mimetype", superType);

			MimeTypeMenu()->AddItem(new IconMenuItem(superMenu, message,
				superType));

			// the MimeTypeMenu's font is not correct at this time
			superMenu->SetFont(be_plain_font);
		}
	}

	if (tracker != NULL) {
		tracker->MimeTypes()->EachCommonType(
			&FindPanel::AddOneMimeTypeToMenu, MimeTypeMenu());
	}

	// remove empty super type menus (and set target)

	for (int32 index = MimeTypeMenu()->CountItems(); index-- > 2;) {
		BMenuItem* item = MimeTypeMenu()->ItemAt(index);
		BMenu* submenu = item->Submenu();
		if (submenu == NULL)
			continue;

		if (submenu->CountItems() == 0) {
			MimeTypeMenu()->RemoveItem(item);
			delete item;
		} else
			submenu->SetTargetForItems(this);
	}
}


void
FindPanel::AddVolumes()
{
	// ToDo: add calls to this to rebuild the menu when a volume gets mounted

	BMessage* message = new BMessage(kVolumeItem);
	message->AddInt32("device", -1);
	fVolMenu->AddItem(new BMenuItem(B_TRANSLATE("All disks"), message));
	fVolMenu->AddSeparatorItem();
	PopUpMenuSetTitle(fVolMenu, B_TRANSLATE("All disks"));

	fFirstVolumeItem = fVolMenu->CountItems();
	fVolumeItemsCount = 0;

	BVolumeRoster roster;
	BVolume volume;
	roster.Rewind();
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsPersistent() && volume.KnowsQuery()) {
			BDirectory root;
			if (volume.GetRootDirectory(&root) != B_OK)
				continue;

			BEntry entry;
			root.GetEntry(&entry);

			Model model(&entry, true);
			if (model.InitCheck() != B_OK)
				continue;

			message = new BMessage(kVolumeItem);
			message->AddInt32("device", volume.Device());
			fVolMenu->AddItem(new ModelMenuItem(&model, model.Name(), message));
			fVolumeItemsCount++;
		}
	}

	if (fVolMenu->ItemAt(0))
		fVolMenu->ItemAt(0)->SetMarked(true);

	fVolMenu->SetTargetForItems(this);
}


BPopUpMenu*
FindPanel::VolMenu(int32* firstVolumeItem, int32* volumeItemsCount) const
{
	if (firstVolumeItem != NULL)
		*firstVolumeItem = fFirstVolumeItem;
	if (volumeItemsCount != NULL)
		*volumeItemsCount = fVolumeItemsCount;
	return fVolMenu;
}


typedef std::pair<entry_ref, uint32> EntryWithDate;

static int
SortByDatePredicate(const EntryWithDate* entry1, const EntryWithDate* entry2)
{
	return entry1->second > entry2->second ?
		-1 : (entry1->second == entry2->second ? 0 : 1);
}


struct AddOneRecentParams {
	BMenu* menu;
	const BMessenger* target;
	uint32 what;
};


static const entry_ref*
AddOneRecentItem(const entry_ref* ref, void* castToParams)
{
	AddOneRecentParams* params = (AddOneRecentParams*)castToParams;

	BMessage* message = new BMessage(params->what);
	message->AddRef("refs", ref);

	char type[B_MIME_TYPE_LENGTH];
	BNode node(ref);
	BNodeInfo(&node).GetType(type);
	BMenuItem* item = new IconMenuItem(ref->name, message, type);
	item->SetTarget(*params->target);
	params->menu->AddItem(item);

	return NULL;
}


static bool
CheckForDuplicates(BObjectList<EntryWithDate, true>* list, EntryWithDate* entry)
{
	// params checking
	if (list == NULL || entry == NULL)
		return false;

	int32 count = list->CountItems();
	for (int32 i = 0; i < count; i++) {
		EntryWithDate* item = list->ItemAt(i);
		if (entry != NULL && item != NULL && item->first == entry->first
			&& entry->second == item->second) {
			return true;
		}
	}

	return false;
}


void
FindPanel::AddRecentQueries(BMenu* menu, bool addSaveAsItem, const BMessenger* target,
	uint32 what, bool includeTemplates)
{
	BObjectList<entry_ref, true> templates(10);
	BObjectList<EntryWithDate, true> recentQueries(10);

	// find all the queries on all volumes
	BVolumeRoster roster;
	BVolume volume;
	roster.Rewind();
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsPersistent() && volume.KnowsQuery() && volume.KnowsAttr()) {
			BQuery query;
			query.SetVolume(&volume);
			query.SetPredicate("_trk/recentQuery == 1");
			if (query.Fetch() != B_OK)
				continue;

			entry_ref ref;
			while (query.GetNextRef(&ref) == B_OK) {
				// ignore queries in the Trash
				BEntry entry(&ref);
				if (FSInTrashDir(&ref) || !entry.Exists())
					continue;

				char type[B_MIME_TYPE_LENGTH];
				BNode node(&ref);
				BNodeInfo(&node).GetType(type);

				if (strcasecmp(type, B_QUERY_TEMPLATE_MIMETYPE) == 0 && includeTemplates) {
					templates.AddItem(new entry_ref(ref));
				} else if (strcasecmp(type, B_QUERY_MIMETYPE) == 0) {
					int32 changeTime;
					if (node.ReadAttr(kAttrQueryLastChange, B_INT32_TYPE, 0, &changeTime,
							sizeof(int32)) == sizeof(int32)) {
						EntryWithDate* item = new EntryWithDate(ref, changeTime);
						if (!CheckForDuplicates(&recentQueries, item)) {
							recentQueries.AddItem(item);
						} else {
							delete item;
						}
					}
				}
			}
		}
	}

	// we are only adding last ten queries
	recentQueries.SortItems(SortByDatePredicate);

	// but all templates
	AddOneRecentParams params;
	params.menu = menu;
	params.target = target;
	params.what = what;
	templates.EachElement(AddOneRecentItem, &params);

	int32 count = recentQueries.CountItems();
	if (count > 10) {
		// show only up to 10 recent queries
		count = 10;
	} else if (count < 0)
		count = 0;

	if (templates.CountItems() > 0 && count > 0)
		menu->AddSeparatorItem();

	for (int32 index = 0; index < count; index++)
		AddOneRecentItem(&recentQueries.ItemAt(index)->first, &params);

	if (addSaveAsItem) {
		// add a Save as template item
		if (count > 0 || templates.CountItems() > 0)
			menu->AddSeparatorItem();

		BMessage* message = new BMessage(kRunSaveAsTemplatePanel);
		BMenuItem* item = new BMenuItem(
			B_TRANSLATE("Save query as template" B_UTF8_ELLIPSIS), message);
		menu->AddItem(item);
	}
}


void
FindPanel::SetupAddRemoveButtons()
{
	BBox* box = dynamic_cast<BBox*>(FindView("Box"));

	ASSERT(box != NULL);

	if (box == NULL)
		return;

	BButton* removeButton = new BButton("remove button", B_TRANSLATE("Remove"),
		new BMessage(kRemoveItem));
	removeButton->SetEnabled(false);
	removeButton->SetTarget(this);

	BButton* addButton = new BButton("add button", B_TRANSLATE("Add"),
		new BMessage(kAddItem));
	addButton->SetTarget(this);

	BGroupLayout* layout = dynamic_cast<BGroupLayout*>(box->GetLayout());

	ASSERT(layout != NULL);

	if (layout == NULL)
		return;

	BLayoutBuilder::Group<>(layout)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(removeButton)
			.Add(addButton)
			.End()
		.End();
}


void
FindPanel::FillCurrentQueryName(BTextControl* queryName, FindWindow* window)
{
	ASSERT(window);
	queryName->SetText(window->QueryName());
}


void
FindPanel::AddAttrRow()
{
	BBox* box = dynamic_cast<BBox*>(FindView("Box"));

	ASSERT(box != NULL);

	if (box == NULL)
		return;

	BGridView* grid = dynamic_cast<BGridView*>(box->FindView("AttrFields"));
	if (grid == NULL) {
		// reset layout
		BLayoutBuilder::Group<>(box, B_VERTICAL);

		grid = new BGridView("AttrFields");
		box->AddChild(grid);
	}

	fAttrGrid = grid->GridLayout();
	fAttrGrid->SetColumnWeight(2, 10);

	AddAttributeControls(fAttrGrid->CountRows());

	// add logic to previous attrview
	if (fAttrGrid->CountRows() > 1)
		AddLogicMenu(fAttrGrid->CountRows() - 2);

	BButton* removeButton = dynamic_cast<BButton*>(
		box->FindView("remove button"));
	if (removeButton != NULL)
		removeButton->SetEnabled(fAttrGrid->CountRows() > 1);
	else
		SetupAddRemoveButtons();
}


void
FindPanel::RemoveAttrRow()
{
	if (fAttrGrid->CountRows() < 2)
		return;

	BView* view;

	int32 row = fAttrGrid->CountRows() - 1;
	for (int32 col = fAttrGrid->CountColumns(); col > 0; col--) {
		BLayoutItem* item = fAttrGrid->ItemAt(col - 1, row);
		if (item == NULL)
			continue;

		view = item->View();
		if (view == NULL)
			continue;

		view->RemoveSelf();
		delete view;
	}

	BString string = "TextEntry";
	string << (row - 1);
	view = FindAttrView(string.String(), row - 1);
	if (view != NULL)
		view->MakeFocus();

	if (fAttrGrid->CountRows() > 1) {
		// remove the And/Or menu field of the previous row
		BLayoutItem* item = fAttrGrid->ItemAt(3, row - 1);
		if (item == NULL)
			return;

		view = item->View();
		if (view == NULL)
			return;

		view->RemoveSelf();
		delete view;
		return;
	}

	// only one row remains

	// disable the remove button
	BButton* button = dynamic_cast<BButton*>(FindView("remove button"));
	if (button != NULL)
		button->SetEnabled(false);

	// remove the And/Or menu field
	BLayoutItem* item = fAttrGrid->RemoveItem(3);
	if (item == NULL)
		return;

	view = item->View();
	if (view == NULL)
		return;

	view->RemoveSelf();
	delete view;
}


uint32
FindPanel::InitialMode(const BNode* node)
{
	if (node == NULL || node->InitCheck() != B_OK)
		return kByNameItem;

	uint32 result;
	if (node->ReadAttr(kAttrQueryInitialMode, B_INT32_TYPE, 0,
		(int32*)&result, sizeof(int32)) <= 0)
		return kByNameItem;

	return result;
}


int32
FindPanel::InitialAttrCount(const BNode* node)
{
	if (node == NULL || node->InitCheck() != B_OK)
		return 1;

	int32 result;
	if (node->ReadAttr(kAttrQueryInitialNumAttrs, B_INT32_TYPE, 0,
		&result, sizeof(int32)) <= 0)
		return 1;

	return result;
}


static int32
SelectItemWithLabel(BMenu* menu, const char* label)
{
	for (int32 index = menu->CountItems(); index-- > 0;)  {
		BMenuItem* item = menu->ItemAt(index);

		if (strcmp(label, item->Label()) == 0) {
			item->SetMarked(true);
			return index;
		}
	}
	return -1;
}


void
FindPanel::SaveWindowState(BNode* node, bool editTemplate)
{
	ASSERT(node->InitCheck() == B_OK);

	BMenuItem* item = CurrentMimeType();
	if (item) {
		BString label(item->Label());
		node->WriteAttrString(kAttrQueryInitialMime, &label);
	}

	uint32 mode = Mode();
	node->WriteAttr(kAttrQueryInitialMode, B_INT32_TYPE, 0,
		(int32*)&mode, sizeof(int32));

	if (editTemplate) {
		if (UserSpecifiedName()) {
			BString name(UserSpecifiedName());
			node->WriteAttrString(kAttrQueryTemplateName, &name);
		}
	}

	switch (Mode()) {
		case kByAttributeItem:
		{
			BMessage message;
			int32 count = fAttrGrid->CountRows();
			node->WriteAttr(kAttrQueryInitialNumAttrs, B_INT32_TYPE, 0,
				&count, sizeof(int32));

			for (int32 index = 0; index < count; index++)
				SaveAttrState(&message, index);

			ssize_t size = message.FlattenedSize();
			if (size > 0) {
				char* buffer = new char[(size_t)size];
				status_t result = message.Flatten(buffer, size);
				if (result == B_OK) {
					node->WriteAttr(kAttrQueryInitialAttrs, B_MESSAGE_TYPE, 0,
						buffer, (size_t)size);
				}
				delete[] buffer;
			}
			break;
		}

		case kByNameItem:
		case kByFormulaItem:
		{
			BTextControl* textControl = dynamic_cast<BTextControl*>(
				FindView("TextControl"));

			ASSERT(textControl != NULL);

			if (textControl != NULL) {
				BString formula(textControl->Text());
				node->WriteAttrString(kAttrQueryInitialString, &formula);
			}
			break;
		}
	}
}


void
FindPanel::SwitchToTemplate(const BNode* node)
{
	SwitchMode(InitialMode(node));
		// update the menu to correspond to the mode
	MarkNamedMenuItem(fSearchModeMenu, InitialMode(node), true);

	if (Mode() == (int32)kByAttributeItem) {
		RemoveByAttributeItems();
		AddByAttributeItems(node);
	}

	RestoreWindowState(node);
}


void
FindPanel::RestoreMimeTypeMenuSelection(const BNode* node)
{
	if (Mode() == (int32)kByFormulaItem || node == NULL
		|| node->InitCheck() != B_OK) {
		return;
	}

	BString buffer;
	if (node->ReadAttrString(kAttrQueryInitialMime, &buffer) == B_OK)
		SetCurrentMimeType(buffer.String());
}


void
FindPanel::RestoreWindowState(const BNode* node)
{
	fMode = InitialMode(node);
	if (node == NULL || node->InitCheck() != B_OK)
		return;

	ShowOrHideMimeTypeMenu();
	RestoreMimeTypeMenuSelection(node);
	MoreOptionsStruct saveMoreOptions;

	bool storesMoreOptions = ReadAttr(node, kAttrQueryMoreOptions,
		kAttrQueryMoreOptionsForeign, B_RAW_TYPE, 0, &saveMoreOptions,
		sizeof(saveMoreOptions), &MoreOptionsStruct::EndianSwap)
			!= kReadAttrFailed;

	if (storesMoreOptions) {
		// need to sanitize to true or false here, could have picked
		// up garbage from attributes

		saveMoreOptions.showMoreOptions = true; // Now unused

		static_cast<FindWindow*>(Window())->SetOptions(saveMoreOptions.searchTrash);
	}

	// get volumes to perform query on

	int32 selectedVolumes = 0;

	attr_info info;
	if (node->GetAttrInfo(kAttrQueryVolume, &info) == B_OK) {
		char* buffer = new char[info.size];
		if (node->ReadAttr(kAttrQueryVolume, B_MESSAGE_TYPE, 0, buffer,
				(size_t)info.size) == info.size) {
			BMessage message;
			if (message.Unflatten(buffer) == B_OK) {
				for (int32 index = 0; ;index++) {
					ASSERT(index < 100);
					BVolume volume;
						// match a volume with the info embedded in
						// the message
					status_t result
						= MatchArchivedVolume(&volume, &message, index);
					if (result == B_OK) {
						char name[256];
						volume.GetName(name);
						if (SelectItemWithLabel(fVolMenu, name) != -1)
							++selectedVolumes;
					} else if (result != B_DEV_BAD_DRIVE_NUM)
						// if B_DEV_BAD_DRIVE_NUM, the volume just isn't
						// mounted this time around, keep looking for more
						// if other error, bail
						break;
				}
			}
		}
		delete[] buffer;
	}

	LoadDirectoryFiltersFromFile(node);
	// mark or unmark "All disks"
	if (selectedVolumes == fVolumeItemsCount) {
		fVolMenu->ItemAt(0)->SetMarked(true);
		for (int32 i = 0; i < fVolumeItemsCount + 2; ++i)
			fVolMenu->ItemAt(i)->SetMarked(false);
	}
	ShowVolumeMenuLabel();

	switch (Mode()) {
		case kByAttributeItem:
		{
			int32 count = InitialAttrCount(node);

			attr_info info;
			if (node->GetAttrInfo(kAttrQueryInitialAttrs, &info) != B_OK)
				break;
			char* buffer = new char[info.size];
			if (node->ReadAttr(kAttrQueryInitialAttrs, B_MESSAGE_TYPE, 0,
					buffer, (size_t)info.size) == info.size) {
				BMessage message;
				if (message.Unflatten(buffer) == B_OK) {
					for (int32 index = 0; index < count; index++)
						RestoreAttrState(message, index);
				}
			}
			delete[] buffer;
			break;
		}

		case kByNameItem:
		case kByFormulaItem:
		{
			BString buffer;
			if (node->ReadAttrString(kAttrQueryInitialString, &buffer)
					== B_OK) {
				BTextControl* textControl = dynamic_cast<BTextControl*>(
					FindView("TextControl"));

				ASSERT(textControl != NULL);

				if (textControl != NULL)
					textControl->SetText(buffer.String());
			}
			break;
		}
	}

	// try to restore focus and possibly text selection
	BString focusedView;
	if (node->ReadAttrString("_trk/focusedView", &focusedView) == B_OK) {
		BView* view = FindView(focusedView.String());
		if (view != NULL) {
			view->MakeFocus();
			BTextControl* textControl = dynamic_cast<BTextControl*>(view);
			if (textControl != NULL && Mode() == kByFormulaItem) {
				int32 selStart = 0;
				int32 selEnd = INT32_MAX;
				node->ReadAttr("_trk/focusedSelStart", B_INT32_TYPE, 0,
					&selStart, sizeof(selStart));
				node->ReadAttr("_trk/focusedSelEnd", B_INT32_TYPE, 0,
					&selEnd, sizeof(selEnd));
				textControl->TextView()->Select(selStart, selEnd);
			}
		}
	}
}


void
FindPanel::AddByAttributeItems(const BNode* node)
{
	int32 numAttributes = InitialAttrCount(node);
	if (numAttributes < 1)
		numAttributes = 1;

	for (int32 index = 0; index < numAttributes; index ++)
		AddAttrRow();
}


void
FindPanel::AddByNameOrFormulaItems()
{
	BBox* box = dynamic_cast<BBox*>(FindView("Box"));

	ASSERT(box != NULL);

	if (box == NULL)
		return;

	// reset layout
	BLayoutBuilder::Group<>(box, B_VERTICAL);

	BTextControl* textControl = new BTextControl("TextControl",
		"", "", NULL);
	textControl->SetDivider(0.0f);
	box->SetBorder(B_NO_BORDER);
	box->AddChild(textControl);
	textControl->MakeFocus();
}


void
FindPanel::RemoveAttrViewItems(bool removeGrid)
{
	if (fAttrGrid == NULL)
		return;

	BView* view = fAttrGrid->View();
	for (int32 index = view->CountChildren(); index > 0; index--) {
		BView* child = view->ChildAt(index - 1);
		child->RemoveSelf();
		delete child;
	}

	if (removeGrid) {
		view->RemoveSelf();
		delete view;
		fAttrGrid = NULL;
	}
}


void
FindPanel::RemoveByAttributeItems()
{
	RemoveAttrViewItems();
	BView* view = FindView("add button");
	if (view) {
		view->RemoveSelf();
		delete view;
	}

	view = FindView("remove button");
	if (view) {
		view->RemoveSelf();
		delete view;
	}

	view = FindView("TextControl");
	if (view) {
		view->RemoveSelf();
		delete view;
	}
}


void
FindPanel::ShowOrHideMimeTypeMenu()
{
	BView* menuFieldSpacer = FindView("MimeTypeMenuSpacer");
	BMenuField* menuField = dynamic_cast<BMenuField*>(FindView("MimeTypeMenu"));
	if (menuFieldSpacer == NULL || menuField == NULL)
		return;

	if (Mode() == (int32)kByFormulaItem && !menuField->IsHidden(this)) {
		BSize size = menuField->ExplicitMinSize();
		menuField->Hide();
		menuFieldSpacer->SetExplicitMinSize(size);
		menuFieldSpacer->SetExplicitMaxSize(size);
		if (menuFieldSpacer->IsHidden(this))
			menuFieldSpacer->Show();
	} else if (menuField->IsHidden(this)) {
		menuFieldSpacer->Hide();
		menuField->Show();
	}
}


void
FindPanel::AddAttributeControls(int32 gridRow)
{
	BPopUpMenu* menu = new BPopUpMenu("PopUp");

	// add NAME attribute to popup
	BMenu* submenu = new BMenu(B_TRANSLATE("Name"));
	submenu->SetRadioMode(true);
	submenu->SetFont(be_plain_font);
	BMessage* message = new BMessage(kAttributeItemMain);
	message->AddString("name", "name");
	message->AddInt32("type", B_STRING_TYPE);
	BMenuItem* item = new BMenuItem(submenu, message);
	menu->AddItem(item);

	for (int32 i = 0; i < 5; i++) {
		message = new BMessage(kAttributeItem);
		message->AddInt32("operator", operators[i]);
		submenu->AddItem(new BMenuItem(
			B_TRANSLATE_NOCOLLECT(operatorLabels[i]), message));
	}

	// mark first items initially
	menu->ItemAt(0)->SetMarked(true);
	submenu->ItemAt(0)->SetMarked(true);

	// add SIZE attribute
	submenu = new BMenu(B_TRANSLATE("Size"));
	submenu->SetRadioMode(true);
	submenu->SetFont(be_plain_font);
	message = new BMessage(kAttributeItemMain);
	message->AddString("name", "size");
	message->AddInt32("type", B_OFF_T_TYPE);
	item = new BMenuItem(submenu, message);
	menu->AddItem(item);

	message = new BMessage(kAttributeItem);
	message->AddInt32("operator", B_GT);
	submenu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(operatorLabels[5]),
		message));

	message = new BMessage(kAttributeItem);
	message->AddInt32("operator", B_LT);
	submenu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(operatorLabels[6]),
		message));

	message = new BMessage(kAttributeItem);
	message->AddInt32("operator", B_EQ);
	submenu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(operatorLabels[1]),
		message));

	// add "modified" field
	submenu = new BMenu(B_TRANSLATE("Modified"));
	submenu->SetRadioMode(true);
	submenu->SetFont(be_plain_font);
	message = new BMessage(kAttributeItemMain);
	message->AddString("name", "last_modified");
	message->AddInt32("type", B_TIME_TYPE);
	item = new BMenuItem(submenu, message);
	menu->AddItem(item);

	message = new BMessage(kAttributeItem);
	message->AddInt32("operator", B_LT);
	submenu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(operatorLabels[7]),
		message));

	message = new BMessage(kAttributeItem);
	message->AddInt32("operator", B_GT);
	submenu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(operatorLabels[8]),
		message));

	BMenuField* menuField = new BMenuField("MenuField", "", menu);
	menuField->SetDivider(0.0f);
	fAttrGrid->AddView(menuField, 0, gridRow);

	BStringView* stringView = new BStringView("",
		menu->FindMarked()->Submenu()->FindMarked()->Label());
	BLayoutItem* layoutItem = fAttrGrid->AddView(stringView, 1, gridRow);
	layoutItem->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_VERTICAL_UNSET));

	BString title("TextEntry");
	title << gridRow;
	BTextControl* textControl = new BTextControl(title.String(), "", "", NULL);
	textControl->SetDivider(0.0f);
	fAttrGrid->AddView(textControl, 2, gridRow);
	textControl->MakeFocus();

	// target everything
	menu->SetTargetForItems(this);
	for (int32 index = menu->CountItems() - 1; index >= 0; index--) {
		BMenu* submenuAtIndex = menu->SubmenuAt(index);
		if (submenuAtIndex != NULL)
			submenuAtIndex->SetTargetForItems(this);
	}

	// populate mime popup
	AddMimeTypeAttrs(menu);
}


void
FindPanel::RestoreAttrState(const BMessage& message, int32 index)
{
	BMenuField* menuField = dynamic_cast<BMenuField*>(FindAttrView("MenuField", index));
	if (menuField != NULL) {
		// decode menu selections
		BMenu* menu = menuField->Menu();

		ASSERT(menu != NULL);

		AddMimeTypeAttrs(menu);
		const char* label;
		if (message.FindString("menuSelection", index, &label) == B_OK) {
			int32 itemIndex = SelectItemWithLabel(menu, label);
			if (itemIndex >= 0) {
				menu = menu->SubmenuAt(itemIndex);
				if (menu != NULL && message.FindString("subMenuSelection",
						index, &label) == B_OK) {
					SelectItemWithLabel(menu, label);
				}
			}
		}
	}

	// decode attribute text
	BString textEntryString = "TextEntry";
	textEntryString << index;
	BTextControl* textControl = dynamic_cast<BTextControl*>(
		FindAttrView(textEntryString.String(), index));

	ASSERT(textControl != NULL);

	const char* string;
	if (textControl != NULL
		&& message.FindString("attrViewText", index, &string) == B_OK) {
		textControl->SetText(string);
	}

	int32 logicMenuSelectedIndex;
	if (message.FindInt32("logicalRelation", index,
			&logicMenuSelectedIndex) == B_OK) {
		BMenuField* field = dynamic_cast<BMenuField*>(
			FindAttrView("Logic", index));
		if (field != NULL) {
			BMenu* fieldMenu = field->Menu();
			if (fieldMenu != NULL) {
				BMenuItem* logicItem
					= fieldMenu->ItemAt(logicMenuSelectedIndex);
				if (logicItem != NULL) {
					logicItem->SetMarked(true);
					return;
				}
			}
		}

		AddLogicMenu(index, logicMenuSelectedIndex == 0);
	}
}


void
FindPanel::SaveAttrState(BMessage* message, int32 index)
{
	BMenu* menu = dynamic_cast<BMenuField*>(FindAttrView("MenuField", index))
		->Menu();

	// encode main attribute menu selection
	BMenuItem* item = menu->FindMarked();
	message->AddString("menuSelection", item ? item->Label() : "");

	// encode submenu selection
	const char* label = "";
	if (item) {
		BMenu* submenu = menu->SubmenuAt(menu->IndexOf(item));
		if (submenu) {
			item = submenu->FindMarked();
			if (item)
				label = item->Label();
		}
	}
	message->AddString("subMenuSelection", label);

	// encode attribute text
	BString textEntryString = "TextEntry";
	textEntryString << index;
	BTextControl* textControl = dynamic_cast<BTextControl*>(FindAttrView(
		textEntryString.String(), index));

	ASSERT(textControl != NULL);

	if (textControl != NULL)
		message->AddString("attrViewText", textControl->Text());

	BMenuField* field = dynamic_cast<BMenuField*>(FindAttrView("Logic", index));
	if (field != NULL) {
		BMenu* fieldMenu = field->Menu();
		if (fieldMenu != NULL) {
			BMenuItem* item = fieldMenu->FindMarked();
			ASSERT(item != NULL);
			message->AddInt32("logicalRelation",
				item != NULL ? field->Menu()->IndexOf(item) : 0);
		}
	}
}


void
FindPanel::AddLogicMenu(int32 index, bool selectAnd)
{
	// add "AND/OR" menu
	BPopUpMenu* menu = new BPopUpMenu("");
	BMessage* message = new BMessage();
	message->AddInt32("combine", B_AND);
	BMenuItem* item = new BMenuItem(B_TRANSLATE("And"), message);
	menu->AddItem(item);
	if (selectAnd)
		item->SetMarked(true);

	message = new BMessage();
	message->AddInt32("combine", B_OR);
	item = new BMenuItem(B_TRANSLATE("Or"), message);
	menu->AddItem(item);
	if (!selectAnd)
		item->SetMarked(true);

	menu->SetTargetForItems(this);

	BMenuField* menufield = new BMenuField("Logic", "", menu, B_WILL_DRAW);
	menufield->SetDivider(0.0f);

	ResizeMenuField(menufield);

	fAttrGrid->AddView(menufield, 3, index);
}


void
FindPanel::RemoveLogicMenu(int32 index)
{
	BMenuField* menufield = dynamic_cast<BMenuField*>(FindAttrView("Logic", index));
	if (menufield) {
		menufield->RemoveSelf();
		delete menufield;
	}
}


void
FindPanel::AddAttributes(BMenu* menu, const BMimeType& mimeType)
{
	// only add things to menu which have "user-visible" data
	BMessage attributeMessage;
	if (mimeType.GetAttrInfo(&attributeMessage) != B_OK)
		return;

	char desc[B_MIME_TYPE_LENGTH];
	mimeType.GetShortDescription(desc);

	// go through each field in meta mime and add it to a menu
	for (int32 index = 0; ; index++) {
		const char* publicName;
		if (attributeMessage.FindString("attr:public_name", index,
				&publicName) != B_OK) {
			break;
		}

		if (!attributeMessage.FindBool("attr:viewable"))
			continue;

		const char* attributeName;
		if (attributeMessage.FindString("attr:name", index, &attributeName)
				!= B_OK) {
			continue;
		}

		int32 type;
		if (attributeMessage.FindInt32("attr:type", index, &type) != B_OK)
			continue;

		BMenu* submenu = new BMenu(publicName);
		submenu->SetRadioMode(true);
		submenu->SetFont(be_plain_font);
		BMessage* message = new BMessage(kAttributeItemMain);
		message->AddString("name", attributeName);
		message->AddInt32("type", type);
		BMenuItem* item = new BMenuItem(submenu, message);
		menu->AddItem(item);
		menu->SetTargetForItems(this);

		switch (type) {
			case B_STRING_TYPE:
				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_CONTAINS);
				submenu->AddItem(new BMenuItem(operatorLabels[0], message));

				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_EQ);
				submenu->AddItem(new BMenuItem(operatorLabels[1], message));

				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_NE);
				submenu->AddItem(new BMenuItem(operatorLabels[2], message));
				submenu->SetTargetForItems(this);

				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_BEGINS_WITH);
				submenu->AddItem(new BMenuItem(operatorLabels[3], message));
				submenu->SetTargetForItems(this);

				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_ENDS_WITH);
				submenu->AddItem(new BMenuItem(operatorLabels[4], message));
				break;

			case B_BOOL_TYPE:
			case B_INT16_TYPE:
			case B_UINT8_TYPE:
			case B_INT8_TYPE:
			case B_UINT16_TYPE:
			case B_INT32_TYPE:
			case B_UINT32_TYPE:
			case B_INT64_TYPE:
			case B_UINT64_TYPE:
			case B_OFF_T_TYPE:
			case B_FLOAT_TYPE:
			case B_DOUBLE_TYPE:
				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_EQ);
				submenu->AddItem(new BMenuItem(operatorLabels[1], message));

				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_GT);
				submenu->AddItem(new BMenuItem(operatorLabels[5], message));

				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_LT);
				submenu->AddItem(new BMenuItem(operatorLabels[6], message));
				break;

			case B_TIME_TYPE:
				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_LT);
				submenu->AddItem(new BMenuItem(operatorLabels[7], message));

				message = new BMessage(kAttributeItem);
				message->AddInt32("operator", B_GT);
				submenu->AddItem(new BMenuItem(operatorLabels[8], message));
				break;
		}
		submenu->SetTargetForItems(this);
	}
}


void
FindPanel::AddMimeTypeAttrs(BMenu* menu)
{
	const char* typeName;
	if (CurrentMimeType(&typeName) == NULL)
		return;

	BMimeType mimeType(typeName);
	if (!mimeType.IsInstalled())
		return;

	if (!mimeType.IsSupertypeOnly()) {
		// add supertype attributes
		BMimeType supertype;
		mimeType.GetSupertype(&supertype);
		AddAttributes(menu, supertype);
	}

	AddAttributes(menu, mimeType);
}


void
FindPanel::GetDefaultAttrName(BString& attrName, int32 row) const
{
	BMenuItem* item = NULL;
	BMenuField* menuField = dynamic_cast<BMenuField*>(fAttrGrid->ItemAt(0, row)->View());
	if (menuField != NULL && menuField->Menu() != NULL)
		item = menuField->Menu()->FindMarked();

	if (item != NULL)
		attrName << item->Label();
	else
		attrName << B_TRANSLATE("Name");

	if (item != NULL && item->Submenu() != NULL)
		item = item->Submenu()->FindMarked();
	else
		item = NULL;

	if (item != NULL)
		attrName << " " << item->Label() << " ";
	else
		attrName << " = ";

	BTextControl* textControl
		= dynamic_cast<BTextControl*>(fAttrGrid->ItemAt(2, row)->View());
	if (textControl != NULL)
		attrName << textControl->Text();
}


// #pragma mark -


DeleteTransientQueriesTask::DeleteTransientQueriesTask()
	:
	state(kInitial),
	fWalker(NULL)
{
}


DeleteTransientQueriesTask::~DeleteTransientQueriesTask()
{
	delete fWalker;
}


bool
DeleteTransientQueriesTask::DoSomeWork()
{
	switch (state) {
		case kInitial:
			Initialize();
			break;

		case kAllocatedWalker:
		case kTraversing:
			if (GetSome()) {
				PRINT(("transient query killer done\n"));
				return true;
			}
			break;

		case kError:
			return true;

	}
	return false;
}


void
DeleteTransientQueriesTask::Initialize()
{
	PRINT(("starting up transient query killer\n"));
	BPath path;
	status_t result = find_directory(B_USER_DIRECTORY, &path, false);
	if (result != B_OK || path.Append("queries") != B_OK) {
		state = kError;
		return;
	}
	fWalker = new BTrackerPrivate::TNodeWalker(path.Path());
	state = kAllocatedWalker;
}


bool
DeleteTransientQueriesTask::GetSome()
{
	state = kTraversing;
	for (int32 count = kBatchCount; count > 0; count--) {
		entry_ref ref;
		if (fWalker->GetNextRef(&ref) != B_OK) {
			state = kError;
			return true;
		}
		Model model(&ref);
		if (model.IsQuery())
			ProcessOneRef(&model);
#if xDEBUG
		else
			PRINT(("transient query killer: %s not a query\n", model.Name()));
#endif
	}
	return false;
}


bool
DeleteTransientQueriesTask::QueryOldEnough(Model* model)
{
	// check if it is old and ready to be deleted
	time_t now = time(0);

	tm nowTimeData;
	tm fileModData;

	localtime_r(&now, &nowTimeData);
	localtime_r(&model->StatBuf()->st_ctime, &fileModData);

	if ((nowTimeData.tm_mday - fileModData.tm_mday) < kDaysToExpire
		&& (nowTimeData.tm_mday - fileModData.tm_mday) > -kDaysToExpire) {
		PRINT(("query %s, not old enough\n", model->Name()));
		return false;
	}
	return true;
}


bool
DeleteTransientQueriesTask::ProcessOneRef(Model* model)
{
	BModelOpener opener(model);

	// is this a temporary query
	if (!MoreOptionsStruct::QueryTemporary(model->Node())) {
		PRINT(("query %s, not temporary\n", model->Name()));
		return false;
	}

	if (!QueryOldEnough(model))
		return false;

	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	ASSERT(tracker != NULL);

	// check that it is not showing
	if (tracker != NULL && tracker->EntryHasWindowOpen(model->EntryRef())) {
		PRINT(("query %s, showing, can't delete\n", model->Name()));
		return false;
	}

	PRINT(("query %s, old, temporary, not shownig - deleting\n",
		model->Name()));

	BEntry entry(model->EntryRef());
	entry.Remove();

	return true;
}


class DeleteTransientQueriesFunctor : public FunctionObjectWithResult<bool> {
public:
								DeleteTransientQueriesFunctor(DeleteTransientQueriesTask* task)
									:
									task(task)
								{}

	virtual 					~DeleteTransientQueriesFunctor() { delete task; }

	virtual	void				operator()() { result = task->DoSomeWork(); }

private:
			DeleteTransientQueriesTask* task;
};


void
DeleteTransientQueriesTask::StartUpTransientQueryCleaner()
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	ASSERT(tracker != NULL);

	if (tracker == NULL)
		return;
	// set up a task that wakes up when the machine is idle and starts
	// killing off old transient queries
	DeleteTransientQueriesFunctor* worker
		= new DeleteTransientQueriesFunctor(new DeleteTransientQueriesTask());

	tracker->MainTaskLoop()->RunWhenIdle(worker,
		30 * 60 * 1000000,	// half an hour initial delay
		5 * 60 * 1000000,	// idle for five minutes
		10 * 1000000);
}


//	#pragma mark -


RecentFindItemsMenu::RecentFindItemsMenu(const char* title,
	const BMessenger* target, uint32 what)
	:
	BMenu(title, B_ITEMS_IN_COLUMN),
	fTarget(*target),
	fWhat(what)
{
}


void
RecentFindItemsMenu::AttachedToWindow()
{
	// re-populate the menu with fresh items
	for (int32 index = CountItems() - 1; index >= 0; index--)
		delete RemoveItem(index);

	FindPanel::AddRecentQueries(this, false, &fTarget, fWhat);
	BMenu::AttachedToWindow();
}


#if !B_BEOS_VERSION_DANO
_IMPEXP_TRACKER
#endif
BMenu*
TrackerBuildRecentFindItemsMenu(const char* title)
{
	BMessenger trackerMessenger(kTrackerSignature);
	return new RecentFindItemsMenu(title, &trackerMessenger, B_REFS_RECEIVED);
}


//	#pragma mark -


DraggableQueryIcon::DraggableQueryIcon(BRect frame, const char* name,
	const BMessage* message, BMessenger messenger, uint32 resizeFlags,
		uint32 flags)
	:
	DraggableIcon(frame, name, B_QUERY_MIMETYPE, B_LARGE_ICON,
		message, messenger, resizeFlags, flags)
{
}


bool
DraggableQueryIcon::DragStarted(BMessage* dragMessage)
{
	// override to substitute the user-specified query name
	dragMessage->RemoveData("be:clip_name");

	FindWindow* window = dynamic_cast<FindWindow*>(Window());

	ASSERT(window != NULL);

	return window != NULL && dragMessage->AddString("be:clip_name",
		window->BackgroundView()->UserSpecifiedName() != NULL
			? window->BackgroundView()->UserSpecifiedName()
			: B_TRANSLATE("New Query")) == B_OK;
}


//	#pragma mark -


MostUsedNames::MostUsedNames(const char* fileName, const char* directory,
	int32 maxCount)
	:
	fFileName(fileName),
	fDirectory(directory),
	fLoaded(false),
	fCount(maxCount)
{
}


MostUsedNames::~MostUsedNames()
{
	// only write back settings when we've been used
	if (!fLoaded)
		return;

	// write most used list to file

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK
		|| path.Append(fDirectory) != B_OK || path.Append(fFileName) != B_OK) {
		return;
	}

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK) {
		for (int32 i = 0; i < fList.CountItems(); i++) {
			list_entry* entry = fList.ItemAt(i);

			// limit upper bound to react more dynamically to changes
			if (--entry->count > 20)
				entry->count = 20;

			// if the item hasn't been chosen in a while, remove it
			// (but leave at least one item in the list)
			if (entry->count < -10 && i > 0)
				continue;

			BString line;
			line.SetToFormat("%" B_PRId32 " %s\n", entry->count, entry->name.String());
			if (file.Write(line.String(), line.Length()) < B_OK)
				break;
		}
	}
	file.Unset();

	// free data

	for (int32 i = fList.CountItems(); i-- > 0;) {
		list_entry* entry = fList.ItemAt(i);
		delete entry;
	}
}


bool
MostUsedNames::ObtainList(BStringList* list)
{
	if (list == NULL)
		return false;

	if (!fLoaded)
		UpdateList();

	fLock.Lock();

	list->MakeEmpty();
	for (int32 i = 0; i < fCount; i++) {
		list_entry* entry = fList.ItemAt(i);
		if (entry == NULL)
			return true;

		list->Add(entry->name);
	}
	return true;
}


void
MostUsedNames::ReleaseList()
{
	fLock.Unlock();
}


void
MostUsedNames::AddName(const BString& name)
{
	fLock.Lock();

	if (!fLoaded)
		LoadList();

	// remove last entry if there are more than
	// 2*fCount entries in the list

	list_entry* entry = NULL;

	if (fList.CountItems() > fCount * 2) {
		entry = fList.RemoveItemAt(fList.CountItems() - 1);

		// is this the name we want to add here?
		if (name == entry->name) {
			delete entry;
			entry = NULL;
		} else
			fList.AddItem(entry);
	}

	if (entry == NULL) {
		for (int32 i = 0; (entry = fList.ItemAt(i)) != NULL; i++) {
			if (entry->name == name)
				break;
		}
	}

	if (entry == NULL) {
		entry = new list_entry;
		entry->name = name;
		entry->count = 1;

		fList.AddItem(entry);
	} else if (entry->count < 0)
		entry->count = 1;
	else
		entry->count++;

	fLock.Unlock();
	UpdateList();
}


int
MostUsedNames::CompareNames(const list_entry* entryA, const list_entry* entryB)
{
	if (entryA->count == entryB->count)
		return entryA->name.ICompare(entryB->name);

	return entryB->count - entryA->count;
}


void
MostUsedNames::LoadList()
{
	if (fLoaded)
		return;
	fLoaded = true;

	// load the most used names list

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK
		|| path.Append(fDirectory) != B_OK || path.Append(fFileName) != B_OK) {
		return;
	}

	FILE* file = fopen(path.Path(), "r");
	if (file == NULL)
		return;

	char line[B_FILE_NAME_LENGTH + 5];
	while (fgets(line, sizeof(line), file) != NULL) {
		int32 length = (int32)strlen(line) - 1;
		if (length >= 0 && line[length] == '\n')
			line[length] = '\0';

		int32 count = atoi(line);

		char* name = strchr(line, ' ');
		if (name == NULL || *(++name) == '\0')
			continue;

		list_entry* entry = new list_entry;
		entry->name = name;
		entry->count = count;

		fList.AddItem(entry);
	}
	fclose(file);
}


void
MostUsedNames::UpdateList()
{
	AutoLock<Benaphore> locker(fLock);

	if (!fLoaded)
		LoadList();

	// sort list items

	fList.SortItems(MostUsedNames::CompareNames);
}

}	// namespace BPrivate
