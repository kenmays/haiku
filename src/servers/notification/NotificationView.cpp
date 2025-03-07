/*
 * Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 * Copyright 2008-2009, Pier Luigi Fiorini. All Rights Reserved.
 * Copyright 2004-2008, Michael Davidson. All Rights Reserved.
 * Copyright 2004-2007, Mikael Eiman. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Davidson, slaad@bong.com.au
 *		Mikael Eiman, mikael@eiman.tv
 *		Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Adrien Destugues <pulkomandy@pulkomandy.ath.cx>
 *		Brian Hill, supernova@tycho.email
 */


#include "NotificationView.h"


#include <Bitmap.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <LayoutUtils.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Notification.h>
#include <NumberFormat.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <Roster.h>
#include <StatusBar.h>

#include <Notifications.h>

#include "AppGroupView.h"
#include "NotificationWindow.h"


const int kIconStripeWidth			= 32;

property_info message_prop_list[] = {
	{ "type", {B_GET_PROPERTY, B_SET_PROPERTY, 0},
		{B_DIRECT_SPECIFIER, 0}, "get the notification type"},
	{ "app", {B_GET_PROPERTY, B_SET_PROPERTY, 0},
		{B_DIRECT_SPECIFIER, 0}, "get notification's app"},
	{ "title", {B_GET_PROPERTY, B_SET_PROPERTY, 0},
		{B_DIRECT_SPECIFIER, 0}, "get notification's title"},
	{ "content", {B_GET_PROPERTY, B_SET_PROPERTY, 0},
		{B_DIRECT_SPECIFIER, 0}, "get notification's contents"},
	{ "icon", {B_GET_PROPERTY, 0},
		{B_DIRECT_SPECIFIER, 0}, "get icon as an archived bitmap"},
	{ "progress", {B_GET_PROPERTY, B_SET_PROPERTY, 0},
		{B_DIRECT_SPECIFIER, 0}, "get the progress (between 0.0 and 1.0)"},

	{ 0 }
};


NotificationView::NotificationView(BNotification* notification, bigtime_t timeout,
	bool disableTimeout)
	:
	BView("NotificationView", B_WILL_DRAW),
	fNotification(notification),
	fTimeout(timeout),
	fIconSize(kDefaultIconSize),
	fDisableTimeout(disableTimeout),
	fGroupView(NULL),
	fRunner(NULL),
	fBitmap(NULL),
	fCloseClicked(false),
	fPreviewModeOn(false)
{
	if (fNotification->Icon() != NULL) {
		fBitmap = new BBitmap(fNotification->Icon());
		fIconSize = fNotification->Icon()->Bounds().IntegerWidth();
	}

	BGroupLayout* layout = new BGroupLayout(B_VERTICAL);
	SetLayout(layout);

	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	switch (fNotification->Type()) {
		case B_IMPORTANT_NOTIFICATION:
			fStripeColor = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
			break;
		case B_ERROR_NOTIFICATION:
			fStripeColor = ui_color(B_FAILURE_COLOR);
			break;
		case B_PROGRESS_NOTIFICATION:
		{
			BStatusBar* progress = new BStatusBar("progress");
			progress->SetBarHeight(12.0f);
			progress->SetMaxValue(1.0f);
			progress->Update(fNotification->Progress());

			BNumberFormat numberFormat;
			BString label;
			double progressPercent = fNotification->Progress();

			if (numberFormat.FormatPercent(label, progressPercent) != B_OK)
				label.SetToFormat("%d%%", (int)(progressPercent * 100));

			progress->SetTrailingText(label.String());
			layout->AddView(progress);
		}
		// fall through.
		case B_INFORMATION_NOTIFICATION:
			fStripeColor = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
				B_DARKEN_1_TINT);
			break;
	}
}


NotificationView::~NotificationView()
{
	delete fRunner;
	delete fBitmap;
	delete fNotification;

	LineInfoList::iterator lIt;
	for (lIt = fLines.begin(); lIt != fLines.end(); lIt++)
		delete (*lIt);
}


void
NotificationView::AttachedToWindow()
{
	SetText();

	fGroupView = dynamic_cast<AppGroupView*>(Parent());

	if (!fDisableTimeout) {
		BMessage msg(kRemoveView);
		msg.AddPointer("view", this);
		fRunner = new BMessageRunner(BMessenger(Parent()), &msg, fTimeout, 1);
	}
}


void
NotificationView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case B_GET_PROPERTY:
		{
			BMessage specifier;
			const char* property;
			BMessage reply(B_REPLY);
			bool msgOkay = true;

			if (msg->FindMessage("specifiers", 0, &specifier) != B_OK)
				msgOkay = false;
			if (specifier.FindString("property", &property) != B_OK)
				msgOkay = false;

			if (msgOkay) {
				if (strcmp(property, "type") == 0)
					reply.AddInt32("result", fNotification->Type());

				if (strcmp(property, "group") == 0)
					reply.AddString("result", fNotification->Group());

				if (strcmp(property, "title") == 0)
					reply.AddString("result", fNotification->Title());

				if (strcmp(property, "content") == 0)
					reply.AddString("result", fNotification->Content());

				if (strcmp(property, "progress") == 0)
					reply.AddFloat("result", fNotification->Progress());

				if ((strcmp(property, "icon") == 0) && fBitmap) {
					BMessage archive;
					if (fBitmap->Archive(&archive) == B_OK)
						reply.AddMessage("result", &archive);
				}

				reply.AddInt32("error", B_OK);
			} else {
				reply.what = B_MESSAGE_NOT_UNDERSTOOD;
				reply.AddInt32("error", B_ERROR);
			}

			msg->SendReply(&reply);
			break;
		}
		case B_SET_PROPERTY:
		{
			BMessage specifier;
			const char* property;
			BMessage reply(B_REPLY);
			bool msgOkay = true;

			if (msg->FindMessage("specifiers", 0, &specifier) != B_OK)
				msgOkay = false;
			if (specifier.FindString("property", &property) != B_OK)
				msgOkay = false;

			if (msgOkay) {
				const char* value = NULL;

				if (strcmp(property, "group") == 0)
					if (msg->FindString("data", &value) == B_OK)
						fNotification->SetGroup(value);

				if (strcmp(property, "title") == 0)
					if (msg->FindString("data", &value) == B_OK)
						fNotification->SetTitle(value);

				if (strcmp(property, "content") == 0)
					if (msg->FindString("data", &value) == B_OK)
						fNotification->SetContent(value);

				if (strcmp(property, "icon") == 0) {
					BMessage archive;
					if (msg->FindMessage("data", &archive) == B_OK) {
						delete fBitmap;
						fBitmap = new BBitmap(&archive);
					}
				}

				SetText();
				Invalidate();

				reply.AddInt32("error", B_OK);
			} else {
				reply.what = B_MESSAGE_NOT_UNDERSTOOD;
				reply.AddInt32("error", B_ERROR);
			}

			msg->SendReply(&reply);
			break;
		}
		default:
			BView::MessageReceived(msg);
	}
}


void
NotificationView::Draw(BRect updateRect)
{
	BRect progRect;

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	BRect stripeRect = Bounds();
	stripeRect.right = kIconStripeWidth;
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_1_TINT));
	FillRect(stripeRect);

	SetHighColor(fStripeColor);
	stripeRect.right = 2;
	FillRect(stripeRect);

	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));

	// Draw icon
	if (fBitmap != NULL) {
		float ix = 18;
		float iy = (Bounds().Height() - fIconSize) / 4.0;
			// Icon is vertically centered in view

		if (fNotification->Type() == B_PROGRESS_NOTIFICATION) {
			// Move icon up by half progress bar height if it's present
			iy -= (progRect.Height() + fGroupView->CloseButtonSize());
		}

		// stretch icon to composed size
		BRect iconRect = fBitmap->Bounds().OffsetByCopy(BPoint(ix, iy));
		DrawBitmapAsync(fBitmap, fBitmap->Bounds(), iconRect);
	}

	// Draw content
	LineInfoList::iterator lIt;
	for (lIt = fLines.begin(); lIt != fLines.end(); lIt++) {
		LineInfo *l = (*lIt);

		SetFont(&l->font);
		// Truncate the string. We have already line-wrapped the text but if
		// there is a very long 'word' we can only truncate it.
		BString text(l->text);
		TruncateString(&text, B_TRUNCATE_END, Bounds().Width() - l->location.x);
		DrawString(text.String(), text.Length(), l->location);
	}

	if (fGroupView != NULL && fGroupView->ChildrenCount() > 1)
		fGroupView->DrawCloseButton(updateRect);

	SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
	BPoint left(Bounds().left, Bounds().top);
	BPoint right(Bounds().right, Bounds().top);
	StrokeLine(left, right);

	Sync();
}


void
NotificationView::MouseDown(BPoint point)
{
	// Preview Mode ignores any mouse clicks
	if (fPreviewModeOn)
		return;

	int32 buttons;
	Window()->CurrentMessage()->FindInt32("buttons", &buttons);

	switch (buttons) {
		case B_PRIMARY_MOUSE_BUTTON:
		{
			BRect closeRect = Bounds().InsetByCopy(2, 2);
			float buttonSize = fGroupView->CloseButtonSize();
			closeRect.left = closeRect.right - buttonSize;
			closeRect.bottom = closeRect.top + buttonSize;

			if (!closeRect.Contains(point)) {
				entry_ref launchRef;
				BString launchString;
				BMessage argMsg(B_ARGV_RECEIVED);
				BMessage refMsg(B_REFS_RECEIVED);
				entry_ref appRef;
				bool useArgv = false;
				BList messages;
				entry_ref ref;

				if (fNotification->OnClickApp() != NULL
					&& be_roster->FindApp(fNotification->OnClickApp(), &appRef)
				   		== B_OK) {
					useArgv = true;
				}

				if (fNotification->OnClickFile() != NULL
					&& be_roster->FindApp(
							(entry_ref*)fNotification->OnClickFile(), &appRef)
				   		== B_OK) {
					useArgv = true;
				}

				for (int32 i = 0; i < fNotification->CountOnClickRefs(); i++)
					refMsg.AddRef("refs", fNotification->OnClickRefAt(i));
				messages.AddItem((void*)&refMsg);

				if (useArgv) {
					int32 argc = fNotification->CountOnClickArgs() + 1;
					BString arg;

					BPath p(&appRef);
					argMsg.AddString("argv", p.Path());

					argMsg.AddInt32("argc", argc);

					for (int32 i = 0; i < argc - 1; i++) {
						argMsg.AddString("argv",
							fNotification->OnClickArgAt(i));
					}

					messages.AddItem((void*)&argMsg);
				}

				if (fNotification->OnClickApp() != NULL)
					be_roster->Launch(fNotification->OnClickApp(), &messages);
				else
					be_roster->Launch(fNotification->OnClickFile(), &messages);
			} else {
				fCloseClicked = true;
			}

			// Remove the info view after a click
			BMessage remove_msg(kRemoveView);
			remove_msg.AddPointer("view", this);

			BMessenger msgr(Parent());
			msgr.SendMessage(&remove_msg);
			break;
		}
	}
}


BHandler*
NotificationView::ResolveSpecifier(BMessage* msg, int32 index, BMessage* spec,
	int32 form, const char* prop)
{
	BPropertyInfo prop_info(message_prop_list);
	if (prop_info.FindMatch(msg, index, spec, form, prop) >= 0) {
		msg->PopSpecifier();
		return this;
	}

	return BView::ResolveSpecifier(msg, index, spec, form, prop);
}


status_t
NotificationView::GetSupportedSuites(BMessage* msg)
{
	msg->AddString("suites", "suite/x-vnd.Haiku-notification_server");
	BPropertyInfo prop_info(message_prop_list);
	msg->AddFlat("messages", &prop_info);
	return BView::GetSupportedSuites(msg);
}


void
NotificationView::SetText(float newMaxWidth)
{
	if (newMaxWidth < 0 && Parent())
		newMaxWidth = Parent()->Bounds().IntegerWidth();
	if (newMaxWidth <= 0)
		newMaxWidth = kDefaultWidth;

	// Delete old lines
	LineInfoList::iterator lIt;
	for (lIt = fLines.begin(); lIt != fLines.end(); lIt++)
		delete (*lIt);
	fLines.clear();

	float iconRight = kIconStripeWidth + fIconSize;
	float buttonSize = fGroupView->CloseButtonSize();

	font_height fh;
	be_bold_font->GetHeight(&fh);
	float fontHeight = ceilf(fh.leading) + ceilf(fh.descent) + ceilf(fh.ascent);
	float y = fontHeight + buttonSize;

	// Title
	LineInfo* titleLine = new LineInfo;
	titleLine->text = fNotification->Title();
	titleLine->font = *be_bold_font;
	titleLine->location = BPoint(iconRight + buttonSize, y);

	if (titleLine->text.Length() > 0) {
		fLines.push_front(titleLine);
		y += fontHeight;
	}

	// Rest of text is rendered with be_plain_font.
	be_plain_font->GetHeight(&fh);
	fontHeight = ceilf(fh.leading) + ceilf(fh.descent) + ceilf(fh.ascent);

	// Split text into chunks between certain characters and compose the lines.
	const char kSeparatorCharacters[] = " \n-\\";
	BString textBuffer = fNotification->Content();
	textBuffer.ReplaceAll("\t", "    ");
	const char* chunkStart = textBuffer.String();
	float maxWidth = newMaxWidth - buttonSize - iconRight;
	LineInfo* line = NULL;
	ssize_t length = textBuffer.Length();
	while (chunkStart - textBuffer.String() < length) {
		size_t chunkLength = strcspn(chunkStart, kSeparatorCharacters) + 1;

		// Start a new line if we didn't start one before
		BString tempText;
		if (line != NULL)
			tempText.SetTo(line->text);
		tempText.Append(chunkStart, chunkLength);

		if (line == NULL || chunkStart[0] == '\n'
			|| StringWidth(tempText) > maxWidth) {
			line = new LineInfo;
			line->font = *be_plain_font;
			line->location = BPoint(iconRight + buttonSize, y);

			fLines.push_front(line);
			y += fontHeight;

			// Skip the eventual new-line character at the beginning of this chunk
			if (chunkStart[0] == '\n') {
				chunkStart++;
				chunkLength--;
			}

			// Skip more new-line characters and move the line further down
			while (chunkStart[0] == '\n') {
				chunkStart++;
				chunkLength--;
				line->location.y += fontHeight;
				y += fontHeight;
			}

			// Strip space at beginning of a new line
			while (chunkStart[0] == ' ') {
				chunkLength--;
				chunkStart++;
			}
		}

		if (chunkStart[0] == '\0')
			break;

		// Append the chunk to the current line, which was either a new
		// line or the one from the previous iteration
		line->text.Append(chunkStart, chunkLength);

		chunkStart += chunkLength;
	}

	fHeight = y + buttonSize;

	// Make sure icon fits
	if (fBitmap != NULL)
		fHeight = std::max(fHeight, fIconSize + buttonSize);

	// Make sure the progress bar is below the text, and the window is big enough.
	static_cast<BGroupLayout*>(GetLayout())->SetInsets(kIconStripeWidth + 8, fHeight, 8, 8);

	_CalculateSize();
}


void
NotificationView::SetPreviewModeOn(bool enabled)
{
	fPreviewModeOn = enabled;
}


const char*
NotificationView::MessageID() const
{
	return fNotification->MessageID();
}


void
NotificationView::_CalculateSize()
{
	float height = fHeight;

	if (fNotification->Type() == B_PROGRESS_NOTIFICATION) {
		font_height fh;
		be_plain_font->GetHeight(&fh);
		float fontHeight = fh.ascent + fh.descent + fh.leading;
		height += fGroupView->CloseButtonSize() + fontHeight * 2;
	}

	SetExplicitMinSize(BSize(0, height));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, height));
}
